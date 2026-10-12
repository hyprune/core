#pragma once
// World format 0.9 paths, seats and keepClear (RFC-0011 "Paths", "Seats and sitting",
// RFC-0004 world 0.9): manifest semantics the schema can't express, the route geometry of
// a path record, and seat references. Authored and user paths share one record shape:
// {id, name?, use?, closed?, points: [{position, type}], speed?, stops?}.
#include "World.hpp"
#include "sim/Route.hpp"
#include <optional>
#include <string>
namespace hyprune::world {
inline constexpr size_t authoredPathLimit = 64, authoredPointLimit = 1024, seatLimit = 64, keepClearLimit = 64,
                        characterInstanceLimit = 16, userCharacterLimit = 8, charactersPerPath = 8;
// The route a path record describes (tool 3's segment geometry).
sim::Route routeOf(const Json &path);
std::string pathUse(const Json &path); // "route", "track" or "generic" (the default)
// Validates and stores paths[], seats[] and keepClear[] of a world 0.9 manifest, and the
// world 0.9 fields of objects[] that refer to them (path, seat, reverse, accent,
// interactions, locked). Object assets must be loaded (traits known). Throws on the first
// violation with a message naming the record.
void loadPaths(World &, const std::string &formatVersion);
// Problems of one path record against the shape rules (points, types, stops by use, arc
// chords); empty when valid. Used for user paths too (paths.set, tool 3).
std::vector<std::string> pathProblems(const Json &path, size_t pointLimit = 256);
// Interactions of an instance (world 0.9): ids, labels, actions and their references.
std::vector<std::string> interactionProblems(const World &, const Json &interactions);
// The arc length of each authored point of a path record.
std::vector<float> pointDistances(const Json &path);
} // namespace hyprune::world
