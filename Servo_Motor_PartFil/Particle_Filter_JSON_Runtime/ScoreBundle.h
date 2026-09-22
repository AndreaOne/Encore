#pragma once

#include <Arduino.h>
#include <FS.h>

constexpr int MAX_SCORE_NOTES = 512;
constexpr int MAX_FLIP_MARKERS = 32;

struct NoteEvent {
  uint32_t startMs;
  uint32_t durationMs;
  float frequencyHz;
  int16_t measure;
  int16_t page;
};

struct FlipMarker {
  uint32_t timeMs;
  int16_t eventIndex;
  int16_t measure;
  int16_t targetPage;
};

struct ScoreBundle {
  uint32_t durationMs = 0;
  int noteCount = 0;
  int markerCount = 0;
  NoteEvent notes[MAX_SCORE_NOTES];
  FlipMarker markers[MAX_FLIP_MARKERS];
};

static inline void resetScoreBundle(ScoreBundle& bundle) {
  bundle.durationMs = 0;
  bundle.noteCount = 0;
  bundle.markerCount = 0;
}

static inline void skipWhitespace(const char*& cursor) {
  while (*cursor == ' ' || *cursor == '\n' || *cursor == '\r' || *cursor == '\t') {
    cursor++;
  }
}

static inline bool consumeChar(const char*& cursor, char expected) {
  skipWhitespace(cursor);
  if (*cursor != expected) {
    return false;
  }
  cursor++;
  return true;
}

static inline bool parseSignedLong(const char*& cursor, long& value) {
  skipWhitespace(cursor);

  bool negative = false;
  if (*cursor == '-') {
    negative = true;
    cursor++;
  }

  if (*cursor < '0' || *cursor > '9') {
    return false;
  }

  long parsed = 0;
  while (*cursor >= '0' && *cursor <= '9') {
    parsed = parsed * 10L + (*cursor - '0');
    cursor++;
  }

  value = negative ? -parsed : parsed;
  return true;
}

static inline const char* findKeyValue(const char* json, const char* key) {
  const char* keyPosition = strstr(json, key);
  if (!keyPosition) {
    return nullptr;
  }

  const char* colon = strchr(keyPosition, ':');
  return colon ? colon + 1 : nullptr;
}

static inline bool parseDurationField(const char* json, ScoreBundle& bundle) {
  const char* cursor = findKeyValue(json, "\"dur\"");
  if (!cursor) {
    return false;
  }

  long durationMs = 0;
  if (!parseSignedLong(cursor, durationMs)) {
    return false;
  }

  bundle.durationMs = durationMs > 0 ? static_cast<uint32_t>(durationMs) : 0;
  return true;
}

static inline bool parseNotesArray(const char* json, ScoreBundle& bundle) {
  const char* cursor = findKeyValue(json, "\"notes\"");
  if (!cursor || !consumeChar(cursor, '[')) {
    return false;
  }

  skipWhitespace(cursor);
  if (*cursor == ']') {
    cursor++;
    return true;
  }

  while (*cursor) {
    long startMs = 0;
    long durationMs = 0;
    long frequencyHundred = 0;
    long measure = 0;
    long page = 0;

    if (!consumeChar(cursor, '[') ||
        !parseSignedLong(cursor, startMs) ||
        !consumeChar(cursor, ',') ||
        !parseSignedLong(cursor, durationMs) ||
        !consumeChar(cursor, ',') ||
        !parseSignedLong(cursor, frequencyHundred) ||
        !consumeChar(cursor, ',') ||
        !parseSignedLong(cursor, measure) ||
        !consumeChar(cursor, ',') ||
        !parseSignedLong(cursor, page) ||
        !consumeChar(cursor, ']')) {
      return false;
    }

    if (bundle.noteCount >= MAX_SCORE_NOTES) {
      return false;
    }

    NoteEvent& note = bundle.notes[bundle.noteCount++];
    note.startMs = startMs > 0 ? static_cast<uint32_t>(startMs) : 0;
    note.durationMs = durationMs > 0 ? static_cast<uint32_t>(durationMs) : 0;
    note.frequencyHz = static_cast<float>(frequencyHundred) / 100.0f;
    note.measure = static_cast<int16_t>(measure);
    note.page = static_cast<int16_t>(page);

    skipWhitespace(cursor);
    if (*cursor == ',') {
      cursor++;
      continue;
    }
    if (*cursor == ']') {
      cursor++;
      return true;
    }
    return false;
  }

  return false;
}

static inline bool parseMarkersArray(const char* json, ScoreBundle& bundle) {
  const char* cursor = findKeyValue(json, "\"markers\"");
  if (!cursor || !consumeChar(cursor, '[')) {
    return false;
  }

  skipWhitespace(cursor);
  if (*cursor == ']') {
    cursor++;
    return true;
  }

  while (*cursor) {
    long timeMs = 0;
    long eventIndex = -1;
    long measure = 0;
    long targetPage = 0;

    if (!consumeChar(cursor, '[') ||
        !parseSignedLong(cursor, timeMs) ||
        !consumeChar(cursor, ',') ||
        !parseSignedLong(cursor, eventIndex) ||
        !consumeChar(cursor, ',') ||
        !parseSignedLong(cursor, measure) ||
        !consumeChar(cursor, ',') ||
        !parseSignedLong(cursor, targetPage) ||
        !consumeChar(cursor, ']')) {
      return false;
    }

    if (bundle.markerCount >= MAX_FLIP_MARKERS) {
      return false;
    }

    FlipMarker& marker = bundle.markers[bundle.markerCount++];
    marker.timeMs = timeMs > 0 ? static_cast<uint32_t>(timeMs) : 0;
    marker.eventIndex = static_cast<int16_t>(eventIndex);
    marker.measure = static_cast<int16_t>(measure);
    marker.targetPage = static_cast<int16_t>(targetPage);

    skipWhitespace(cursor);
    if (*cursor == ',') {
      cursor++;
      continue;
    }
    if (*cursor == ']') {
      cursor++;
      return true;
    }
    return false;
  }

  return false;
}

static inline bool loadScoreBundleFromJson(const char* rawJson, ScoreBundle& bundle) {
  resetScoreBundle(bundle);

  if (!rawJson || !*rawJson) {
    return false;
  }

  if (!parseDurationField(rawJson, bundle)) {
    return false;
  }
  if (!parseNotesArray(rawJson, bundle)) {
    return false;
  }
  if (!parseMarkersArray(rawJson, bundle)) {
    return false;
  }

  if (bundle.noteCount <= 0) {
    return false;
  }

  if (bundle.durationMs == 0) {
    const NoteEvent& lastNote = bundle.notes[bundle.noteCount - 1];
    bundle.durationMs = lastNote.startMs + lastNote.durationMs;
  }

  return true;
}

static inline bool loadScoreBundle(fs::FS& filesystem, const char* path, ScoreBundle& bundle) {
  File file = filesystem.open(path, FILE_READ);
  if (!file) {
    return false;
  }

  String json = file.readString();
  file.close();

  return loadScoreBundleFromJson(json.c_str(), bundle);
}
