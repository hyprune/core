#include "Characters.hpp"
#include "Machines.hpp"
#include "Objects.hpp"
#include "Pacing.hpp"
#include "Sandbox.hpp"
#include "VkContext.hpp"
#include "Watchdog.hpp"
#include "interaction/Config.hpp"
#include "render/Renderer.hpp"
#include <cstring>
#include <drm_fourcc.h>
#include <fcntl.h>
#include <gbm.h>
#include <iostream>
#include <poll.h>
#include <random>
#include <signal.h>
#include <thread>
#include <sys/prctl.h>
using namespace hyprune;
using namespace hyprune::offload;
struct Image {
    VkImage image{};
    VkDeviceMemory memory{};
    VkSubresourceLayout layout{};
    int fd = -1;
    gbm_bo *bo = nullptr;
};
int main(int argc, char **argv) {
    // HYPRUNE_DEBUG_RENDERER (passed through by hypruned): progress on stderr.
    const bool debug = getenv("HYPRUNE_DEBUG_RENDERER") != nullptr;
    // HYPRUNE_DEV_WORKER_COST_MS="mean[:jitter]" (dev and nested tests only, passed through by
    // hypruned): every frame takes at least that long (normally distributed), a GPU-bound
    // worker that can't keep the policy's rate (a 4K output under a power cap).
    double costMean = 0, costJitter = 0;
    if (auto c = getenv("HYPRUNE_DEV_WORKER_COST_MS")) {
        costMean = std::max(0., std::atof(c));
        if (auto colon = std::strchr(c, ':'))
            costJitter = std::max(0., std::atof(colon + 1));
    }
    std::mt19937 costRandom(1);
    auto note = [&](const std::string &what) {
        if (debug)
            std::cerr << "Vulkan worker: " << what << '\n';
    };
    try {
        check(argc >= 8, "usage: renderer FD WORLD DRM WIDTH HEIGHT FPS CONTROL [OBJECT_ROOT...]");
        note(std::string("start on ") + argv[3]);
        prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
        // Catalog object roots (library and user objects) are readable like the package.
        sandbox(argv[2], {argv + 8, argv + argc});
        Watchdog watchdog(std::stoi(argv[7]));
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        int sock = std::stoi(argv[1]), w = std::stoi(argv[4]), h = std::stoi(argv[5]), fps = std::stoi(argv[6]);
        constexpr bool reverse = false;
        check(w > 0 && h > 0 && w <= 3840 && h <= 2160 && fps > 0 && fps <= 60, "safety bounds");
        Context gl(argv[3]);
        auto extensions = std::string(reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS)));
        for (auto required : {"GL_EXT_memory_object_fd", "GL_EXT_semaphore_fd"})
            check(extensions.find(required) != std::string::npos, required);
        Vulkan vk(argv[3]);
        int drm = -1;
        gbm_device *gbm = nullptr;
        auto allocate = [&](bool source) -> Image {
            Image b;
            uint64_t modifier = 0;
            VkExternalMemoryImageCreateInfo ei{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
            ei.handleTypes =
                source ? VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT : VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
            VkImageDrmFormatModifierListCreateInfoEXT li{
                VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT};
            li.pNext = &ei;
            li.drmFormatModifierCount = 1;
            li.pDrmFormatModifiers = &modifier;
            VkImageDrmFormatModifierExplicitCreateInfoEXT ex{
                VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT};
            ex.pNext = &ei;
            ex.drmFormatModifier = 0;
            ex.drmFormatModifierPlaneCount = 1;
            ex.pPlaneLayouts = &b.layout;
            if (!source && reverse) {
                b.bo =
                    gbm_bo_create_with_modifiers2(gbm, w, h, DRM_FORMAT_ABGR8888, &modifier, 1, GBM_BO_USE_RENDERING);
                if (!b.bo)
                    b.bo = gbm_bo_create(gbm, w, h, DRM_FORMAT_ABGR8888, GBM_BO_USE_LINEAR | GBM_BO_USE_RENDERING);
                check(b.bo && gbm_bo_get_modifier(b.bo) == 0, "Intel LINEAR target");
                b.fd = gbm_bo_get_fd(b.bo);
                b.layout.rowPitch = gbm_bo_get_stride(b.bo);
                b.layout.offset = gbm_bo_get_offset(b.bo, 0);
            }
            VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            ci.pNext = source    ? static_cast<void *>(&ei)
                       : reverse ? static_cast<void *>(&ex)
                                 : static_cast<void *>(&li);
            ci.imageType = VK_IMAGE_TYPE_2D;
            ci.format = VK_FORMAT_R8G8B8A8_UNORM;
            ci.extent = {uint32_t(w), uint32_t(h), 1};
            ci.mipLevels = ci.arrayLayers = 1;
            ci.samples = VK_SAMPLE_COUNT_1_BIT;
            ci.tiling = source ? VK_IMAGE_TILING_OPTIMAL : VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;
            ci.usage = source ? (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
                              : VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            vkcheck(vkCreateImage(vk.device, &ci, nullptr, &b.image), "image");
            VkMemoryRequirements req;
            vkGetImageMemoryRequirements(vk.device, b.image, &req);
            uint32_t bits = req.memoryTypeBits;
            VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
            dedicated.image = b.image;
            VkExportMemoryAllocateInfo exportInfo{VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO};
            exportInfo.pNext = &dedicated;
            exportInfo.handleTypes = ei.handleTypes;
            VkImportMemoryFdInfoKHR importInfo{VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR};
            importInfo.pNext = &dedicated;
            importInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
            importInfo.fd = b.fd;
            if (b.bo) {
                VkMemoryFdPropertiesKHR prop{VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR};
                auto get = reinterpret_cast<PFN_vkGetMemoryFdPropertiesKHR>(
                    vkGetDeviceProcAddr(vk.device, "vkGetMemoryFdPropertiesKHR"));
                vkcheck(get(vk.device, importInfo.handleType, b.fd, &prop), "import bits");
                bits &= prop.memoryTypeBits;
            }
            VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            ai.pNext = b.bo ? static_cast<void *>(&importInfo) : static_cast<void *>(&exportInfo);
            ai.allocationSize = req.size;
            ai.memoryTypeIndex = vk.type(bits);
            vkcheck(vkAllocateMemory(vk.device, &ai, nullptr, &b.memory), "allocate image memory");
            if (b.bo)
                b.fd = -1;
            vkcheck(vkBindImageMemory(vk.device, b.image, b.memory, 0), "bind image");
            if (b.bo)
                b.fd = gbm_bo_get_fd(b.bo);
            else {
                VkMemoryGetFdInfoKHR fi{VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR};
                fi.memory = b.memory;
                fi.handleType = static_cast<VkExternalMemoryHandleTypeFlagBits>(ei.handleTypes);
                vkcheck(reinterpret_cast<PFN_vkGetMemoryFdKHR>(vkGetDeviceProcAddr(vk.device, "vkGetMemoryFdKHR"))(
                            vk.device, &fi, &b.fd),
                        "export image fd");
            }
            if (!source && !b.bo) {
                VkImageSubresource sub{VK_IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT, 0, 0};
                vkGetImageSubresourceLayout(vk.device, b.image, &sub, &b.layout);
            }
            if (source)
                b.layout.size = req.size;
            return b;
        };
        auto semaphore = [&](VkExternalSemaphoreHandleTypeFlagBits type) {
            VkExportSemaphoreCreateInfo ei{VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO};
            ei.handleTypes = type;
            VkSemaphoreCreateInfo ci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            ci.pNext = &ei;
            VkSemaphore sem;
            vkcheck(vkCreateSemaphore(vk.device, &ci, nullptr, &sem), "semaphore");
            return sem;
        };
        auto fdSemaphore = [&](VkSemaphore sem, VkExternalSemaphoreHandleTypeFlagBits type) {
            VkSemaphoreGetFdInfoKHR info{VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR};
            info.semaphore = sem;
            info.handleType = type;
            int fd = -1;
            vkcheck(reinterpret_cast<PFN_vkGetSemaphoreFdKHR>(vkGetDeviceProcAddr(vk.device, "vkGetSemaphoreFdKHR"))(
                        vk.device, &info, &fd),
                    "semaphore fd");
            return fd;
        };
        auto glSemaphore = [&](VkSemaphore sem) {
            GLuint s;
            proc<PFNGLGENSEMAPHORESEXTPROC>("glGenSemaphoresEXT")(1, &s);
            proc<PFNGLIMPORTSEMAPHOREFDEXTPROC>("glImportSemaphoreFdEXT")(
                s, GL_HANDLE_TYPE_OPAQUE_FD_EXT, fdSemaphore(sem, VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT));
            return s;
        };
        struct Slot {
            Image source, dest;
            GLuint memory = 0, tex = 0, fbo = 0, readyGL = 0, doneGL = 0;
            VkSemaphore ready{}, done{}, native{};
            VkCommandBuffer command{};
            VkQueryPool query{};
            Timestamps timer;
            int acquire = -1;
            Json request;
            bool pending = false;
            double started = 0; // when its pose was read (now(), ms)
        };
        Slot slots[2];
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(vk.physical, &properties);
        auto barrier = [&](VkCommandBuffer cmd, VkImage image, VkImageLayout before, VkImageLayout after, uint32_t from,
                           uint32_t to, VkAccessFlags src, VkAccessFlags dst) {
            VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            b.image = image;
            b.oldLayout = before;
            b.newLayout = after;
            b.srcQueueFamilyIndex = from;
            b.dstQueueFamilyIndex = to;
            b.srcAccessMask = src;
            b.dstAccessMask = dst;
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0,
                                 nullptr, 0, nullptr, 1, &b);
        };
        send(sock, {{"type", "caps"},
                    {"renderer", reinterpret_cast<const char *>(glGetString(GL_RENDERER))},
                    {"vulkan", vk.caps},
                    {"transport", reverse ? "Intel allocation -> Vulkan copy -> Intel dmabuf"
                                          : "NVIDIA Vulkan LINEAR -> Intel dmabuf"},
                    {"renderSize", {w, h}},
                    {"transferSize", {w, h}},
                    {"graphics", render::effectiveGraphics(render::graphicsDefaults(),
                                                           reinterpret_cast<const char *>(glGetString(GL_RENDERER)))},
                    {"interop", "GL_EXT_memory_object_fd + GL_EXT_semaphore_fd"}});
        for (int i = 0; i < 2; i++) {
            auto &b = slots[i];
            b.source = allocate(true);
            b.dest = allocate(false);
            proc<PFNGLCREATEMEMORYOBJECTSEXTPROC>("glCreateMemoryObjectsEXT")(1, &b.memory);
            GLint dedicated = GL_TRUE;
            proc<PFNGLMEMORYOBJECTPARAMETERIVEXTPROC>("glMemoryObjectParameterivEXT")(
                b.memory, GL_DEDICATED_MEMORY_OBJECT_EXT, &dedicated);
            proc<PFNGLIMPORTMEMORYFDEXTPROC>("glImportMemoryFdEXT")(b.memory, b.source.layout.size,
                                                                    GL_HANDLE_TYPE_OPAQUE_FD_EXT, b.source.fd);
            b.source.fd = -1;
            glGenTextures(1, &b.tex);
            glBindTexture(GL_TEXTURE_2D, b.tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_TILING_EXT, GL_OPTIMAL_TILING_EXT);
            proc<PFNGLTEXSTORAGEMEM2DEXTPROC>("glTexStorageMem2DEXT")(GL_TEXTURE_2D, 1, GL_RGBA8, w, h, b.memory, 0);
            check(glGetError() == GL_NO_ERROR, "GL external memory texture");
            b.fbo = framebuffer(b.tex);
            b.ready = semaphore(VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT);
            b.done = semaphore(VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT);
            b.native = semaphore(VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT);
            b.readyGL = glSemaphore(b.ready);
            b.doneGL = glSemaphore(b.done);
            b.timer.init();
            VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            ai.commandPool = vk.pool;
            ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            ai.commandBufferCount = 1;
            VkCommandBuffer init;
            vkAllocateCommandBuffers(vk.device, &ai, &init);
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            vkBeginCommandBuffer(init, &begin);
            barrier(init, b.source.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, vk.family,
                    VK_QUEUE_FAMILY_EXTERNAL, 0, 0);
            barrier(init, b.dest.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, vk.family,
                    vk.foreign ? VK_QUEUE_FAMILY_FOREIGN_EXT : VK_QUEUE_FAMILY_EXTERNAL, 0, 0);
            vkEndCommandBuffer(init);
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &init;
            submit.signalSemaphoreCount = 1;
            submit.pSignalSemaphores = &b.ready;
            vkcheck(vkQueueSubmit(vk.queue, 1, &submit, VK_NULL_HANDLE), "init submit");
            vkQueueWaitIdle(vk.queue);
            vkFreeCommandBuffers(vk.device, vk.pool, 1, &init);
            VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
            qi.queryCount = 2;
            vkCreateQueryPool(vk.device, &qi, nullptr, &b.query);
            vkAllocateCommandBuffers(vk.device, &ai, &b.command);
            vkBeginCommandBuffer(b.command, &begin);
            vkCmdResetQueryPool(b.command, b.query, 0, 2);
            barrier(b.command, b.source.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_QUEUE_FAMILY_EXTERNAL, vk.family, 0, VK_ACCESS_TRANSFER_READ_BIT);
            barrier(b.command, b.dest.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    vk.foreign ? VK_QUEUE_FAMILY_FOREIGN_EXT : VK_QUEUE_FAMILY_EXTERNAL, vk.family, 0,
                    VK_ACCESS_TRANSFER_WRITE_BIT);
            vkCmdWriteTimestamp(b.command, VK_PIPELINE_STAGE_TRANSFER_BIT, b.query, 0);
            VkImageCopy copy{};
            copy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.dstSubresource = copy.srcSubresource;
            copy.extent = {uint32_t(w), uint32_t(h), 1};
            vkCmdCopyImage(b.command, b.source.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, b.dest.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            vkCmdWriteTimestamp(b.command, VK_PIPELINE_STAGE_TRANSFER_BIT, b.query, 1);
            barrier(b.command, b.source.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, vk.family,
                    VK_QUEUE_FAMILY_EXTERNAL, VK_ACCESS_TRANSFER_READ_BIT, 0);
            barrier(b.command, b.dest.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, vk.family,
                    vk.foreign ? VK_QUEUE_FAMILY_FOREIGN_EXT : VK_QUEUE_FAMILY_EXTERNAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                    0);
            vkEndCommandBuffer(b.command);
            send(sock,
                 {{"type", "buffer"},
                  {"slot", i},
                  {"width", w},
                  {"height", h},
                  {"format", DRM_FORMAT_ABGR8888},
                  {"modifier", 0},
                  {"stride", b.dest.layout.rowPitch},
                  {"offset", b.dest.layout.offset}},
                 {b.dest.fd});
        }
        // World 0.9 library references resolve in the object roots the plugin passed.
        world::LoadOptions loadOptions{render::Renderer::compressedFormats()};
        for (int k = 8; k < argc; ++k)
            loadOptions.libraryRoots.push_back(argv[k]);
        auto scene = world::load(argv[2], loadOptions);
        render::Renderer renderer;
        renderer.applyGraphics(render::graphicsDefaults());
        renderer.worldOnly = true;
        renderer.prepare(scene);
        // World objects follow the plugin's state (Objects.hpp); catalog assets decode on a
        // thread and upload between frames, like the in-process renderer.
        offload::ObjectReceiver objects;
        objects.start(*scene);
        offload::MachineReceiver machines;
        const unsigned formats = render::Renderer::compressedFormats();
        offload::ObjectReceiver::Loader loadAsset = [formats, motion = scene->motionPacks](const std::filesystem::path &dir,
                                                                                        const Json &manifest) {
            world::LoadOptions options{formats};
            options.motion = motion; // the scene's motion packs: the same clips as the plugin's
            return world::loadObjectAsset(dir, manifest, options);
        };
        // Assets drawable here (merged and uploaded): announced whenever the count grows, so
        // the plugin poses again for a still scene once a mid-session asset is ready.
        size_t announced = scene->objectAssets.size();
        auto announce = [&] {
            send(sock, {{"type", "assets"}, {"ready", announced}, {"failed", objects.failures()}});
        };
        announce();
        send(sock, {{"type", "ready"}});
        note("ready");
        offload::RateCap cap; // the watchdog's FPS as an average, not a spacing (Pacing.hpp)
        Json lastGraphics, lastGraphicsRaw, lastProfile;
        uint64_t sequence = 0;
        GLenum layout = GL_LAYOUT_GENERAL_EXT;
        for (;;) {
            fps = watchdog.fps();
            for (auto &b : slots)
                if (b.pending) {
                    pollfd p{b.acquire, POLLIN, 0};
                    if (b.acquire >= 0) {
                        int result = poll(&p, 1, 0);
                        check(result >= 0, "Vulkan fence poll");
                        if (!result)
                            continue;
                        check(p.revents & POLLIN, "Vulkan fence failure");
                    }
                    uint64_t t[2]{};
                    vkcheck(vkGetQueryPoolResults(vk.device, b.query, 0, 2, sizeof(t), t, sizeof(uint64_t),
                                                  VK_QUERY_RESULT_64_BIT),
                            "copy timestamps");
                    Json reply = b.request;
                    reply["type"] = "frame";
                    reply["renderGpuMs"] = b.timer.between(0, 1);
                    reply["transferGpuMs"] = (t[1] - t[0]) * properties.limits.timestampPeriod / 1e6;
                    reply["producerReadyMs"] = now() - reply["sent"].get<double>();
                    reply["workerDone"] = now();
                    reply["workerStart"] = b.started; // began this frame (its pose read)
                    auto timing = renderer.timings();
                    reply["renderScale"] = timing["renderScale"];
                    reply["msaaSamples"] = timing["msaaSamples"];
                    reply["gpuEffectsMs"] = timing["gpuEffectsMs"];
                    reply["objectDraw"] = timing["objects"];
                    reply["objectsLoading"] = objects.loading() || renderer.objectAssetsPending();
                    reply["assetsFailed"] = objects.failures();
                    reply["machines"] = machines.diagnostics();
                    reply["machines"]["labelsDrawn"] = renderer.worldLabelsDrawn;
                    if (b.acquire < 0)
                        b.acquire = fence(gl.display);
                    send(sock, reply, {b.acquire});
                    close(b.acquire);
                    b.acquire = -1;
                    b.pending = false;
                }
            if (objects.merge(*scene, !renderer.objectAssetsPending()))
                renderer.stageObjectAssets();
            renderer.continueObjectAssets();
            if (!renderer.objectAssetsPending() && scene->objectAssets.size() != announced) {
                announced = scene->objectAssets.size();
                note("assets ready " + std::to_string(announced));
                announce();
            }
            // Keep retiring completed slots while waiting for the next capped
            // submission. Sleeping after dequeue used to delay frame replies.
            if (!cap.ready(now(), fps)) {
                usleep(500);
                continue;
            }
            pollfd socket{sock, POLLIN, 0};
            check(poll(&socket, 1, 1) >= 0, "socket poll");
            if (!(socket.revents & POLLIN)) {
                check(!(socket.revents & (POLLHUP | POLLERR)), "parent EOF");
                continue;
            }
            Json p;
            std::vector<int> fds;
            if (!receive(sock, p, fds))
                continue;
            for (int fd : fds)
                close(fd);
            check(fds.empty(), "unexpected worker descriptors");
            if (p["type"] == "stop")
                break;
            check(p["version"] == 2 && p["type"] == "pose", "protocol");
            note("pose " + std::to_string(p.value("sequence", uint64_t(0))) + " (" + std::to_string(p.dump().size()) + " bytes)");
            // Validated only when it changed: building and checking the whole config cost a few
            // ms of every frame's latency.
            if (p.at("graphics") != lastGraphicsRaw) {
                auto graphics =
                    interaction::configPatch(interaction::defaultConfig(), {{"graphics", p.at("graphics")}}).at("graphics");
                lastGraphicsRaw = p.at("graphics");
                if (graphics != lastGraphics) {
                    renderer.applyGraphics(graphics);
                    renderer.intelQuality = false;
                    lastGraphics = graphics;
                }
            }
            if (p.at("profile") != lastProfile) {
                renderer.profile = p.at("profile");
                lastProfile = p.at("profile");
            }
            double frameStart = now();
            int i = p["slot"];
            check(i >= 0 && i < 2 && !slots[i].pending, "slot ownership");
            check(p["sequence"].get<uint64_t>() > sequence, "sequence");
            sequence = p["sequence"];
            auto &b = slots[i];
            b.started = now();
            cap.spend();
            if (p.contains("objects") && !p["objects"].is_null()) {
                objects.apply(p["objects"], scene->objectAssets.size(), loadAsset);
                renderer.transforms = objects.transforms;
            }
            objects.materialize(*scene);
            // Machines: the plugin runtime's draw state, on this world's instances.
            if (p.contains("machines") && !p["machines"].is_null())
                machines.apply(p["machines"], *scene);
            machines.materialize(*scene, renderer);
            p.erase("machines");
            p["machinesRevision"] = machines.revision();
            // Characters: posed here from the plugin's pose inputs, uploaded before drawing.
            offload::applyCharacters(p.value("characters", Json::array()), *scene, renderer);
            renderer.prepareCharacters(SIZE_MAX); // the worker's own process: no compositor frame to protect
            p.erase("characters");
            if (p.contains("clock") && p["clock"].is_number())
                renderer.animationClock = p["clock"].get<double>();
            if (p.contains("logicalScale") && p["logicalScale"].is_number())
                renderer.logicalScale = std::clamp(p["logicalScale"].get<float>(), .25f, 4.f);
            p.erase("objects");
            p["objectsRevision"] = objects.revision();
            b.request = p;
            sim::Camera camera;
            auto pose = p["pose"];
            camera.position = {pose[0], pose[1], pose[2]};
            camera.yaw = pose[3];
            camera.pitch = pose[4];
            camera.fov = pose[5];
            if(pose.size()>=9) { camera.roll=pose[6];camera.shiftX=pose[7];camera.shiftY=pose[8]; }
            proc<PFNGLWAITSEMAPHOREEXTPROC>("glWaitSemaphoreEXT")(b.readyGL, 0, nullptr, 1, &b.tex, &layout);
            b.timer.stamp(0);
            auto count = renderer.frames;
            renderer.draw(b.fbo, w, h, camera, {}, false);
            check(renderer.frames == count + 1, "reused frame");
            b.timer.stamp(1);
            proc<PFNGLSIGNALSEMAPHOREEXTPROC>("glSignalSemaphoreEXT")(b.doneGL, 0, nullptr, 1, &b.tex, &layout);
            glFlush();
            check(glGetError() == GL_NO_ERROR, "GL interop render");
            VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            VkSemaphore signals[] = {b.ready, b.native};
            VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
            submit.waitSemaphoreCount = 1;
            submit.pWaitSemaphores = &b.done;
            submit.pWaitDstStageMask = &stage;
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &b.command;
            submit.signalSemaphoreCount = 2;
            submit.pSignalSemaphores = signals;
            vkcheck(vkQueueSubmit(vk.queue, 1, &submit, VK_NULL_HANDLE), "copy submit");
            b.acquire = fdSemaphore(b.native, VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT);
            b.pending = true;
            if (costMean > 0) {
                double cost = std::max(1., std::normal_distribution<double>(costMean, costJitter)(costRandom));
                std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(frameStart + cost - now()));
            }
        }
        // Child teardown is process-local; kernel/driver retire any submitted resources.
        renderer.destroy();
        vkQueueWaitIdle(vk.queue);
        for (auto &b : slots) {
            if (b.acquire >= 0)
                close(b.acquire);
            glDeleteFramebuffers(1, &b.fbo);
            glDeleteTextures(1, &b.tex);
            proc<PFNGLDELETEMEMORYOBJECTSEXTPROC>("glDeleteMemoryObjectsEXT")(1, &b.memory);
            proc<PFNGLDELETESEMAPHORESEXTPROC>("glDeleteSemaphoresEXT")(1, &b.readyGL);
            proc<PFNGLDELETESEMAPHORESEXTPROC>("glDeleteSemaphoresEXT")(1, &b.doneGL);
            b.timer.destroy();
            vkDestroySemaphore(vk.device, b.ready, nullptr);
            vkDestroySemaphore(vk.device, b.done, nullptr);
            vkDestroySemaphore(vk.device, b.native, nullptr);
            vkDestroyQueryPool(vk.device, b.query, nullptr);
            for (auto *image : {&b.source, &b.dest}) {
                if (image->fd >= 0)
                    close(image->fd);
                vkDestroyImage(vk.device, image->image, nullptr);
                vkFreeMemory(vk.device, image->memory, nullptr);
                if (image->bo)
                    gbm_bo_destroy(image->bo);
            }
        }
        if (gbm)
            gbm_device_destroy(gbm);
        if (drm >= 0)
            close(drm);
        close(sock);
    } catch (const std::exception &e) {
        std::cerr << "Vulkan worker: " << e.what() << '\n';
        return 1;
    }
}
