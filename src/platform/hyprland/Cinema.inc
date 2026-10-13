// Demo camera driver (sim/Cinema.hpp) for nested recordings: hl.plugin.hyprune.cinema(json),
// registered only outside dev/live builds. Not a wire method. While a path, follow or blend
// moves the camera, every tick that follows a present evaluates it at the presented-frame
// clock and frames keep coming (frameForced).
std::optional<sim::cinema::Subject> Runtime::cinemaSubject(const std::string &id) {
    for (auto &n : npcs)
        if (n.id == id)
            return sim::cinema::Subject{n.agent.position, n.agent.heading};
    if (world)
        for (auto &o : world->objectInstances)
            if (o.id == id && !o.hidden) {
                Vec3 z{o.model.v[8], o.model.v[9], o.model.v[10]};
                return sim::cinema::Subject{{o.model.v[12], o.model.v[13], o.model.v[14]}, std::atan2(z.x, z.z)};
            }
    return std::nullopt;
}
double Runtime::refreshInterval() {
    auto m = monitor.lock();
    return 1. / (m && m->m_refreshRate > 1 ? double(m->m_refreshRate) : 60.);
}
void Runtime::exportStart(const std::string &path, int width, int height, size_t queueBytes, int every) {
    exportStop();
    frameExport.start(path, width, height, queueBytes, every);
    exportPasses = 0;
    exportUnticked = 0;
    presentsTicked = presents;
    // World animations follow the frame count too (unless a test pinned their clock).
    exportPinnedAnimation = !renderer.animationClock;
    exportAnimation = renderer.animationClock.value_or(renderer.steadyAnimationSeconds());
}
void Runtime::exportStop() {
    if (!frameExport.active())
        return;
    Render::GL::g_pHyprOpenGL->makeEGLCurrent();
    frameExport.stop();
    if (exportPinnedAnimation)
        renderer.animationClock.reset();
    exportPinnedAnimation = false;
}
void Runtime::cinemaTick(float tickDt) {
    // The camera moves by whole refresh intervals, counted on a virtual vblank that is
    // phase-locked to the presents: a present that comes a few ms late or early (headless
    // and nested outputs jitter) still moves exactly one interval, a missed one two. An
    // extra tick between presents moves nothing; with nothing presented (idle timer), tick time.
    double dt = tickDt, period = refreshInterval();
    auto seconds = [](std::chrono::steady_clock::time_point p) { return std::chrono::duration<double>(p.time_since_epoch()).count(); };
    if (frameExport.active()) {
        cinemaClock = presentedAt; // tick time is already counted in presented frames
        cinemaVsync = seconds(presentedAt);
    } else if (presentedAt != cinemaClock) {
        double at = seconds(presentedAt), frames = std::round((at - cinemaVsync) / period);
        if (cinemaClock == std::chrono::steady_clock::time_point{} || frames < 0 || frames > 6) {
            cinemaVsync = at; // first present, or a long gap: start counting again
            dt = cinemaClock == std::chrono::steady_clock::time_point{} ? 0 : std::min(double(tickDt), 6 * period);
        } else {
            dt = frames * period;
            cinemaVsync += dt;
            cinemaVsync += (at - cinemaVsync) * .05; // follow the real refresh's drift
        }
        cinemaClock = presentedAt;
    } else if (std::chrono::steady_clock::now() - lastPresented < std::chrono::milliseconds(30))
        dt = 0;
    dt = std::clamp(dt, 0., .1);
    std::function<Vec3(Vec3, Vec3)> clip;
    if (cinemaCollide && world)
        clip = [this](Vec3 target, Vec3 eye) {
            auto d = eye - target;
            float len = length(d);
            if (len < 1e-3f)
                return eye;
            if (auto hit = interaction::raycast(target, d / len, *world); hit && hit->distance < len + .3f)
                return target + d / len * std::max(.2f, hit->distance - .3f);
            return eye;
        };
    auto pose = cinema.tick(dt, [this](const std::string &id) { return cinemaSubject(id); }, baseFov, clip);
    if (!pose)
        return;
    camera.position = pose->eye;
    camera.yaw = pose->yaw;
    camera.pitch = std::clamp(pose->pitch, -1.48f, 1.48f);
    camera.fov = std::max(3.f, pose->fov);
    camera.verticalSpeed = 0;
    camera.viewBob = {};
    camera.climbBob = 0;
    if (cinemaTracing && cinemaTrace.size() < 100000)
        cinemaTrace.push_back({std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(), dt,
                               tickDt, pose->eye.x, pose->eye.y, pose->eye.z, camera.yaw, camera.pitch});
}
Json Runtime::cinemaCommand(const Json &request) {
    using namespace sim::cinema;
    auto op = request.value("op", "status");
    auto vec = [](const Json &j) { return Vec3{j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>()}; };
    Pose current{camera.position, camera.yaw, camera.pitch, camera.fov};
    double blend = request.value("blendMs", 0.) / 1000;
    auto started = [&] {
        climbing.reset(camera);
        keys.clear();
        smoothIntent = {};
        cinemaClock = presentedAt;
        cinemaVsync = std::chrono::duration<double>(presentedAt.time_since_epoch()).count();
        if (request.value("trace", false)) {
            cinemaTrace.clear();
            cinemaTracing = true;
        }
    };
    if (op == "path") {
        std::vector<Key> track;
        for (auto &k : request.at("keys")) {
            Key key;
            key.t = k.at("t").get<double>();
            key.eye = vec(k.at("eye"));
            if (k.contains("target"))
                key.target = vec(k["target"]);
            key.yaw = k.value("yaw", 0.f);
            key.pitch = k.value("pitch", 0.f);
            if (k.contains("fov"))
                key.fov = k["fov"].get<float>();
            static const std::map<std::string, Ease> eases{{"none", Ease::None}, {"in", Ease::In}, {"out", Ease::Out}, {"inout", Ease::InOut}};
            auto e = k.value("ease", "none");
            if (!eases.contains(e))
                throw std::invalid_argument("ease: none, in, out or inout");
            key.ease = eases.at(e);
            track.push_back(key);
        }
        cinema.path(Track(std::move(track), baseFov), blend, current);
        cinemaCollide = false;
        started();
    } else if (op == "follow") {
        FollowParams p;
        p.back = request.value("back", p.back);
        p.side = request.value("side", p.side);
        p.height = request.value("height", p.height);
        p.ahead = request.value("ahead", p.ahead);
        p.lookHeight = request.value("lookHeight", p.lookHeight);
        p.halfLife = request.value("halfLife", p.halfLife);
        p.lookHalfLife = request.value("lookHalfLife", p.lookHalfLife);
        p.headingHalfLife = request.value("headingHalfLife", p.headingHalfLife);
        p.snap = request.value("snap", p.snap);
        if (request.contains("fov"))
            p.fov = request["fov"].get<float>();
        static const std::map<std::string, FollowParams::Frame> frames{
            {"heading", FollowParams::Frame::Heading}, {"world", FollowParams::Frame::World}, {"fixed", FollowParams::Frame::Fixed}};
        auto frame = request.value("frame", "heading");
        if (!frames.contains(frame))
            throw std::invalid_argument("frame: heading, world or fixed");
        p.frame = frames.at(frame);
        if (p.frame != FollowParams::Frame::Heading) {
            p.offset = vec(request.at("offset"));
            p.lookOffset = request.contains("lookOffset") ? vec(request["lookOffset"]) : Vec3{0, p.lookHeight, 0};
        }
        auto subject = request.at("target").get<std::string>();
        if (!cinemaSubject(subject))
            throw std::invalid_argument("no character or object " + subject);
        cinema.follow(subject, p, blend, current);
        cinemaCollide = request.value("collide", false);
        started();
    } else if (op == "export") {
        // Raw RGBX frames, top row first, one per presented frame of this output, into
        // path (a FIFO an encoder reads at the refresh rate, or a file).
        auto m = monitor.lock();
        if (!m)
            throw std::invalid_argument("no output");
        int w = request.value("width", int(m->m_pixelSize.x)), h = request.value("height", int(m->m_pixelSize.y));
        exportStart(request.at("path").get<std::string>(), w, h, size_t(request.value("queueMB", 2048)) << 20, request.value("every", 1));
    } else if (op == "exportStop") {
        exportStop();
    } else if (op == "stop") {
        cinema.stop();
        cinemaTracing = false;
    } else if (op == "trace") {
        Json samples = Json::array();
        for (auto &s : cinemaTrace)
            samples.push_back(Json(std::vector<double>(s.begin(), s.end())));
        if (request.value("clear", false))
            cinemaTrace.clear();
        return {{"fields", {"t", "dt", "tickDt", "x", "y", "z", "yaw", "pitch"}}, {"samples", samples}};
    } else if (op != "status")
        throw std::invalid_argument("op: path, follow, stop, status or trace");
    static const char *modes[] = {"idle", "hold", "path", "follow"};
    Json out = {{"mode", modes[int(cinema.mode())]},
                {"time", cinema.time()},
                {"duration", cinema.duration()},
                {"done", cinema.done()},
                {"moving", cinema.moving()},
                {"blending", cinema.blending()},
                {"subject", cinema.subject()},
                {"lost", cinema.lost()},
                {"traced", cinemaTrace.size()}};
    auto e = frameExport.status();
    out["export"] = {{"active", e.active}, {"grabbed", e.grabbed}, {"written", e.written}, {"skipped", e.skipped},
                     {"stalls", e.stalls}, {"queued", e.queued}, {"passes", exportPasses}, {"unticked", exportUnticked}, {"error", e.error},
                     {"draining", e.draining},
                     {"size", {e.width, e.height}}};
    out["progress"] = cinema.duration() > 0 ? std::min(1., cinema.time() / cinema.duration()) : cinema.done() ? 1. : 0.;
    if (auto p = cinema.pose())
        out["pose"] = {{"eye", {p->eye.x, p->eye.y, p->eye.z}}, {"yaw", p->yaw}, {"pitch", p->pitch}, {"fov", p->fov}};
    return out;
}
