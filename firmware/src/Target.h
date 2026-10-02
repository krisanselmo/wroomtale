#pragma once
#include <Arduino.h>

// What a tag, the console or the panel can name: a folder, one track, a story
// folder, or a clip embedded in flash. One place reads the spelling, so the
// tag map, the portal and the player all agree on it.
namespace Target {
constexpr char BUILTIN_PREFIX[] = "builtin:";
// Optional: a folder holding a manifest is a story without it.
constexpr char STORY_PREFIX[] = "story:";

inline bool isBuiltin(const String &target) { return target.startsWith(BUILTIN_PREFIX); }
inline String builtinId(const String &target) { return target.substring(strlen(BUILTIN_PREFIX)); }

inline String stripStoryPrefix(const String &target) {
	return target.startsWith(STORY_PREFIX) ? target.substring(strlen(STORY_PREFIX)) : target;
}

inline bool isMp3(const String &name) {
	String lower = name;
	lower.toLowerCase();
	return lower.endsWith(".mp3");
}

// One track rather than a folder: one tag, one sound.
inline bool isFile(const String &target) { return !isBuiltin(target) && isMp3(target); }

// openNextFile() may hand back a bare name or a full path.
inline String baseName(const String &path) {
	const int slash = path.lastIndexOf('/');
	return slash >= 0 ? path.substring(slash + 1) : path;
}
} // namespace Target
