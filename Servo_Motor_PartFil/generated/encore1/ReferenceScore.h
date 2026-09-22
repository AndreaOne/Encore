#pragma once

#include <Arduino.h>
#include <FS.h>

#include <cstring>

constexpr int MAX_SCORE_NOTES = 512;
constexpr int MAX_FLIP_MARKERS = 32;

struct ScoreNote {
  uint32_t startMs;
  uint32_t durationMs;
  float frequencyHz;
  int16_t measure;
  int16_t page;
};

struct ScoreFlipMarker {
  uint32_t timeMs;
  int16_t eventIndex;
  int16_t measure;
  int16_t targetPage;
};

struct ReferenceScore {
  uint32_t durationMs = 0;
  int noteCount = 0;
  int markerCount = 0;
  ScoreNote notes[MAX_SCORE_NOTES];
  ScoreFlipMarker markers[MAX_FLIP_MARKERS];
};

static inline void resetReferenceScore(ReferenceScore& score) {
  score.durationMs = 0;
  score.noteCount = 0;
  score.markerCount = 0;
}

static inline void skipScoreWhitespace(const char*& cursor) {
  while (*cursor == ' ' || *cursor == '\n' || *cursor == '\r' || *cursor == '\t') {
    cursor++;
  }
}

static inline bool consumeScoreChar(const char*& cursor, char expected) {
  skipScoreWhitespace(cursor);
  if (*cursor != expected) {
    return false;
  }
  cursor++;
  return true;
}

static inline bool parseSignedLongValue(const char*& cursor, long& value) {
  skipScoreWhitespace(cursor);

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

static inline const char* findJsonValue(const char* json, const char* key) {
  const char* keyPosition = strstr(json, key);
  if (!keyPosition) {
    return nullptr;
  }

  const char* colon = strchr(keyPosition, ':');
  return colon ? colon + 1 : nullptr;
}

static inline bool parseDurationField(const char* json, ReferenceScore& score) {
  const char* cursor = findJsonValue(json, "\"dur\"");
  if (!cursor) {
    return false;
  }

  long durationMs = 0;
  if (!parseSignedLongValue(cursor, durationMs)) {
    return false;
  }

  score.durationMs = durationMs > 0 ? static_cast<uint32_t>(durationMs) : 0;
  return true;
}

static inline bool parseNotesArray(const char* json, ReferenceScore& score) {
  const char* cursor = findJsonValue(json, "\"notes\"");
  if (!cursor || !consumeScoreChar(cursor, '[')) {
    return false;
  }

  skipScoreWhitespace(cursor);
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

    if (!consumeScoreChar(cursor, '[') ||
        !parseSignedLongValue(cursor, startMs) ||
        !consumeScoreChar(cursor, ',') ||
        !parseSignedLongValue(cursor, durationMs) ||
        !consumeScoreChar(cursor, ',') ||
        !parseSignedLongValue(cursor, frequencyHundred) ||
        !consumeScoreChar(cursor, ',') ||
        !parseSignedLongValue(cursor, measure) ||
        !consumeScoreChar(cursor, ',') ||
        !parseSignedLongValue(cursor, page) ||
        !consumeScoreChar(cursor, ']')) {
      return false;
    }

    if (score.noteCount >= MAX_SCORE_NOTES) {
      return false;
    }

    ScoreNote& note = score.notes[score.noteCount++];
    note.startMs = startMs > 0 ? static_cast<uint32_t>(startMs) : 0;
    note.durationMs = durationMs > 0 ? static_cast<uint32_t>(durationMs) : 0;
    note.frequencyHz = static_cast<float>(frequencyHundred) / 100.0f;
    note.measure = static_cast<int16_t>(measure);
    note.page = static_cast<int16_t>(page);

    skipScoreWhitespace(cursor);
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

static inline bool parseMarkersArray(const char* json, ReferenceScore& score) {
  const char* cursor = findJsonValue(json, "\"markers\"");
  if (!cursor || !consumeScoreChar(cursor, '[')) {
    return false;
  }

  skipScoreWhitespace(cursor);
  if (*cursor == ']') {
    cursor++;
    return true;
  }

  while (*cursor) {
    long timeMs = 0;
    long eventIndex = -1;
    long measure = 0;
    long targetPage = 0;

    if (!consumeScoreChar(cursor, '[') ||
        !parseSignedLongValue(cursor, timeMs) ||
        !consumeScoreChar(cursor, ',') ||
        !parseSignedLongValue(cursor, eventIndex) ||
        !consumeScoreChar(cursor, ',') ||
        !parseSignedLongValue(cursor, measure) ||
        !consumeScoreChar(cursor, ',') ||
        !parseSignedLongValue(cursor, targetPage) ||
        !consumeScoreChar(cursor, ']')) {
      return false;
    }

    if (score.markerCount >= MAX_FLIP_MARKERS) {
      return false;
    }

    ScoreFlipMarker& marker = score.markers[score.markerCount++];
    marker.timeMs = timeMs > 0 ? static_cast<uint32_t>(timeMs) : 0;
    marker.eventIndex = static_cast<int16_t>(eventIndex);
    marker.measure = static_cast<int16_t>(measure);
    marker.targetPage = static_cast<int16_t>(targetPage);

    skipScoreWhitespace(cursor);
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

static inline bool loadReferenceScoreFromJson(const char* rawJson, ReferenceScore& score) {
  resetReferenceScore(score);

  if (!rawJson || !*rawJson) {
    return false;
  }

  if (!parseDurationField(rawJson, score)) {
    return false;
  }

  if (!parseNotesArray(rawJson, score)) {
    return false;
  }

  if (!parseMarkersArray(rawJson, score)) {
    return false;
  }

  if (score.noteCount <= 0) {
    return false;
  }

  if (score.durationMs == 0) {
    const ScoreNote& lastNote = score.notes[score.noteCount - 1];
    score.durationMs = lastNote.startMs + lastNote.durationMs;
  }

  return true;
}

static inline bool loadReferenceScore(fs::FS& filesystem, const char* path, ReferenceScore& score) {
  File file = filesystem.open(path, FILE_READ);
  if (!file) {
    return false;
  }

  String json = file.readString();
  file.close();

  return loadReferenceScoreFromJson(json.c_str(), score);
}

static inline float referencePitchAt(const ReferenceScore& score, float positionMs) {
  for (int index = 0; index < score.noteCount; index++) {
    const ScoreNote& note = score.notes[index];
    float noteStartMs = static_cast<float>(note.startMs);
    float noteEndMs = noteStartMs + static_cast<float>(note.durationMs);

    if (positionMs < noteStartMs) {
      break;
    }

    if (positionMs >= noteStartMs && positionMs < noteEndMs) {
      return note.frequencyHz;
    }
  }

  return 0.0f;
}

static inline const ScoreFlipMarker* advanceFlipMarker(
  const ReferenceScore& score,
  float estimatedMs,
  int& nextMarkerIndex
) {
  if (nextMarkerIndex < 0 || nextMarkerIndex >= score.markerCount) {
    return nullptr;
  }

  const ScoreFlipMarker& marker = score.markers[nextMarkerIndex];
  if (estimatedMs < static_cast<float>(marker.timeMs)) {
    return nullptr;
  }

  nextMarkerIndex++;
  return &marker;
}
