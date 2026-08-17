#pragma once

#include <string>

// Project-root-relative paths.
//
// Everything used to be hardcoded to "c:\0_CODE\Dogma75\...", which meant the
// project only ran correctly from that exact directory. Nothing failed at
// BUILD time — settings silently stopped loading, the font failed, logs went
// to a stale location — which is the worst way for it to break.
//
// appRoot() resolves the project root once, from the running executable's
// location, by walking up until it finds a directory containing "settings".
// That works both for build\Release\MinimalDAW.exe and for a flat deployment
// with settings\ beside the exe.
const std::string& appRoot();

// Join a project-root-relative path, e.g. appPath("settings\\user_settings.json").
std::string appPath(const std::string& relative);
