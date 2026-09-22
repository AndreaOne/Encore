const state = {
  file: null,
  parsedScore: null,
  bundleTemplates: new Map(),
  bundle: null,
  selectedPartId: null,
  pageRowCount: 0,
  sourcePageRanges: [],
  disabledSourcePages: new Set(),
  visualPickSourcePage: null,
  visualPickMeasureIndex: null,
  turnDrafts: new Map(),
  measureHotspots: [],
  osmd: null,
};

const dom = {
  fileInput: document.querySelector("#fileInput"),
  exportButton: document.querySelector("#exportButton"),
  resetMarkersButton: document.querySelector("#resetMarkersButton"),
  addTurnButton: document.querySelector("#addTurnButton"),
  partSelect: document.querySelector("#partSelect"),
  scoreCanvas: document.querySelector("#scoreCanvas"),
  scoreSurface: document.querySelector("#scoreSurface"),
  scoreOverlay: document.querySelector("#scoreOverlay"),
  scoreTitle: document.querySelector("#scoreTitle"),
  scoreMeta: document.querySelector("#scoreMeta"),
  exportPath: document.querySelector("#exportPath"),
  downloadLink: document.querySelector("#downloadLink"),
  noteCount: document.querySelector("#noteCount"),
  measureCount: document.querySelector("#measureCount"),
  pageCount: document.querySelector("#pageCount"),
  markerCount: document.querySelector("#markerCount"),
  turnList: document.querySelector("#turnList"),
  turnHint: document.querySelector("#turnHint"),
  statusBar: document.querySelector("#statusBar"),
};

function cloneBundle(bundle) {
  return JSON.parse(JSON.stringify(bundle));
}

function setStatus(message, tone = "neutral") {
  dom.statusBar.textContent = message;
  dom.statusBar.dataset.tone = tone;
}

function formatMs(ms) {
  const seconds = (ms / 1000).toFixed(2);
  return `${seconds}s`;
}

function freqHundredToNoteName(freqHundred) {
  const frequency = freqHundred / 100;
  if (!frequency) {
    return "Rest";
  }
  const midi = Math.round(69 + (12 * Math.log2(frequency / 440)));
  const names = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
  const noteName = names[((midi % 12) + 12) % 12];
  const octave = Math.floor(midi / 12) - 1;
  return `${noteName}${octave}`;
}

function sortMarkers() {
  if (!state.bundle) {
    return;
  }
  state.bundle.markers.sort((left, right) => left[3] - right[3] || left[0] - right[0]);
}

function clamp(value, lower, upper) {
  return Math.min(Math.max(value, lower), upper);
}

function getAllMeasureEntries() {
  if (!state.bundle) {
    return [];
  }

  return state.bundle.measures.map((measure, measureIndex) => ({
    measureIndex,
    measure,
    ordinal: measureIndex + 1,
  }));
}

function buildSourcePageRangesFromBundle(bundle) {
  if (!bundle?.measures?.length) {
    return [];
  }

  const ranges = [];
  let startMeasureIndex = 0;
  let currentPage = bundle.measures[0][3] || 1;

  for (let measureIndex = 1; measureIndex < bundle.measures.length; measureIndex += 1) {
    const nextPage = bundle.measures[measureIndex][3] || currentPage;
    if (nextPage === currentPage) {
      continue;
    }

    ranges.push({
      startMeasureIndex,
      endMeasureIndex: measureIndex - 1,
    });
    startMeasureIndex = measureIndex;
    currentPage = nextPage;
  }

  return ranges;
}

function normalizeSourcePageRanges(bundle, requestedCount, seedRanges = []) {
  const measureCount = bundle?.measures?.length || 0;
  if (!measureCount || requestedCount <= 0) {
    return [];
  }

  const sourcePageCount = Math.min(requestedCount, Math.max(0, measureCount - 1));
  const normalized = [];
  let startMeasureIndex = 0;

  for (let sourcePage = 1; sourcePage <= sourcePageCount; sourcePage += 1) {
    const remainingSourcePages = sourcePageCount - sourcePage;
    const minEndMeasureIndex = startMeasureIndex;
    const maxEndMeasureIndex = measureCount - remainingSourcePages - 1;
    const totalRemainingPages = remainingSourcePages + 1;
    const availableMeasures = measureCount - startMeasureIndex;
    const balancedPageSize = Math.max(1, Math.round(availableMeasures / totalRemainingPages));
    const defaultEndMeasureIndex = startMeasureIndex + balancedPageSize - 1;
    const preferredEnd = seedRanges[sourcePage - 1]?.endMeasureIndex ?? defaultEndMeasureIndex;
    const endMeasureIndex = clamp(preferredEnd, minEndMeasureIndex, maxEndMeasureIndex);

    normalized.push({
      startMeasureIndex,
      endMeasureIndex,
    });
    startMeasureIndex = endMeasureIndex + 1;
  }

  return normalized;
}

function applySourcePageRangesToBundle(bundle, seedRanges = state.sourcePageRanges) {
  if (!bundle?.measures?.length) {
    state.sourcePageRanges = [];
    return;
  }

  state.sourcePageRanges = normalizeSourcePageRanges(bundle, state.pageRowCount, seedRanges);

  const finalPageNumber = state.sourcePageRanges.length + 1;

  bundle.measures.forEach((measure, measureIndex) => {
    let pageNumber = finalPageNumber;

    state.sourcePageRanges.forEach((range, rangeIndex) => {
      if (measureIndex >= range.startMeasureIndex && measureIndex <= range.endMeasureIndex) {
        pageNumber = rangeIndex + 1;
      }
    });

    measure[3] = pageNumber;

    for (let eventIndex = measure[4]; eventIndex <= measure[5]; eventIndex += 1) {
      if (eventIndex >= 0 && bundle.notes[eventIndex]) {
        bundle.notes[eventIndex][4] = pageNumber;
      }
    }
  });

  bundle.pages = buildPagesFromMeasures(bundle.measures);

  const maxTargetPage = bundle.pages.length;
  bundle.markers = bundle.markers.filter((marker) => marker[3] <= maxTargetPage);
  sortMarkers();
}

function getSourcePageRange(sourcePage) {
  return state.sourcePageRanges[sourcePage - 1] || null;
}

function getRangeLabel(range) {
  if (!range || !state.bundle) {
    return "Range not set yet.";
  }

  const startMeasureExists = Boolean(state.bundle.measures[range.startMeasureIndex]);
  const endMeasureExists = Boolean(state.bundle.measures[range.endMeasureIndex]);
  if (!startMeasureExists || !endMeasureExists) {
    return "Range not set yet.";
  }

  if (range.startMeasureIndex === range.endMeasureIndex) {
    return `Covers ${getDisplayBarLabel(range.startMeasureIndex)} only.`;
  }

  return `Covers ${getDisplayBarLabel(range.startMeasureIndex)} to ${getDisplayBarLabel(range.endMeasureIndex)}.`;
}

function getSourcePageRangeLabel(sourcePage) {
  return getRangeLabel(getSourcePageRange(sourcePage));
}

function getFinalPageRange() {
  if (!state.bundle?.measures?.length) {
    return null;
  }

  const lastConfiguredRange = state.sourcePageRanges[state.sourcePageRanges.length - 1] || null;
  const startMeasureIndex = lastConfiguredRange ? lastConfiguredRange.endMeasureIndex + 1 : 0;
  const endMeasureIndex = state.bundle.measures.length - 1;

  if (startMeasureIndex > endMeasureIndex) {
    return null;
  }

  return {
    startMeasureIndex,
    endMeasureIndex,
  };
}

function getRangeEndOptionsForSourcePage(sourcePage) {
  if (!state.bundle?.measures?.length) {
    return [];
  }

  const range = getSourcePageRange(sourcePage);
  if (!range) {
    return [];
  }

  const remainingSourcePages = state.pageRowCount - sourcePage;
  const minEndMeasureIndex = range.startMeasureIndex;
  const maxEndMeasureIndex = state.bundle.measures.length - remainingSourcePages - 1;
  const options = [];

  for (let measureIndex = minEndMeasureIndex; measureIndex <= maxEndMeasureIndex; measureIndex += 1) {
    const measure = state.bundle.measures[measureIndex];
    if (!measure) {
      continue;
    }
    options.push({
      measureIndex,
      measure,
      ordinal: measureIndex + 1,
    });
  }

  return options;
}

function getMaxSourcePageCount(bundle = state.bundle) {
  const measureCount = bundle?.measures?.length || 0;
  return Math.max(0, measureCount - 1);
}

function pruneDisabledSourcePages() {
  const nextDisabledPages = new Set();

  state.disabledSourcePages.forEach((sourcePage) => {
    if (sourcePage >= 1 && sourcePage <= state.pageRowCount) {
      nextDisabledPages.add(sourcePage);
    }
  });

  state.disabledSourcePages = nextDisabledPages;
}

function getAllPitchedMeasures() {
  if (!state.bundle) {
    return [];
  }

  const measures = [];
  let ordinal = 0;

  state.bundle.measures.forEach((measure, measureIndex) => {
    if (measure[4] < 0 || measure[5] < 0) {
      return;
    }

    ordinal += 1;
    measures.push({
      measureIndex,
      measure,
      ordinal,
    });
  });

  return measures;
}

function getMeasuresOnPage(pageNumber) {
  if (!state.bundle) {
    return [];
  }

  const measures = [];
  let ordinal = 0;

  state.bundle.measures.forEach((measure, measureIndex) => {
    if (measure[3] !== pageNumber || measure[4] < 0 || measure[5] < 0) {
      return;
    }

    ordinal += 1;
    measures.push({
      measureIndex,
      measure,
      ordinal,
    });
  });

  return measures;
}

function getMeasuresForSourcePage(sourcePage) {
  const pageMeasures = getMeasuresOnPage(sourcePage);
  if (pageMeasures.length) {
    return {
      mode: "page",
      entries: pageMeasures,
    };
  }

  return {
    mode: "score",
    entries: getAllPitchedMeasures(),
  };
}

function hasDisplayNoteheads() {
  return Boolean(state.bundle?.noteheads?.length);
}

function getEventsForMeasureIndex(measureIndex) {
  if (!state.bundle || measureIndex == null) {
    return [];
  }

  if (hasDisplayNoteheads()) {
    const events = [];
    state.bundle.noteheads.forEach((notehead, eventIndex) => {
      if (!notehead || notehead[5] !== measureIndex) {
        return;
      }

      events.push({
        eventIndex,
        note: notehead,
      });
    });
    return events;
  }

  const measure = state.bundle.measures[measureIndex];
  if (!measure || measure[4] < 0 || measure[5] < 0) {
    return [];
  }

  const events = [];
  for (let eventIndex = measure[4]; eventIndex <= measure[5]; eventIndex += 1) {
    events.push({
      eventIndex,
      note: state.bundle.notes[eventIndex],
    });
  }
  return events;
}

function getEventBySelectionIndex(eventIndex) {
  if (eventIndex == null || eventIndex < 0) {
    return null;
  }

  if (hasDisplayNoteheads()) {
    const note = state.bundle?.noteheads?.[eventIndex];
    return note ? { eventIndex, note } : null;
  }

  const note = state.bundle?.notes?.[eventIndex];
  return note ? { eventIndex, note } : null;
}

function getCompactEventIndexForTime(timeMs) {
  if (!state.bundle?.notes?.length) {
    return -1;
  }

  let nearestIndex = 0;
  let nearestDistance = Number.POSITIVE_INFINITY;

  for (let index = 0; index < state.bundle.notes.length; index += 1) {
    const note = state.bundle.notes[index];
    const noteStart = note[0];
    const noteEnd = noteStart + note[1];

    if (timeMs >= noteStart && timeMs < noteEnd) {
      return index;
    }

    const distance = Math.abs(timeMs - noteStart);
    if (distance < nearestDistance) {
      nearestDistance = distance;
      nearestIndex = index;
    }
  }

  return nearestIndex;
}

function getDisplayBarNumber(measureIndex) {
  return measureIndex + 1;
}

function getDisplayBarLabel(measureIndex) {
  return `Bar ${getDisplayBarNumber(measureIndex)}`;
}

function getMeasureLabel(entry, mode) {
  const barLabel = getDisplayBarLabel(entry.measureIndex);
  return mode === "page"
    ? `${barLabel} • item ${entry.ordinal} on page`
    : `${barLabel} • item ${entry.ordinal} in score`;
}

function getNoteLabel(entry, indexInMeasure) {
  return `${indexInMeasure + 1}. ${freqHundredToNoteName(entry.note[2])}`;
}

function buildPagesFromMeasures(measures) {
  if (!measures.length) {
    return [];
  }

  const pages = [];
  let currentPage = measures[0][3];
  let firstMeasure = measures[0][0];
  let lastMeasure = measures[0][0];

  for (let index = 1; index < measures.length; index += 1) {
    const measure = measures[index];
    if (measure[3] !== currentPage) {
      pages.push([currentPage, firstMeasure, lastMeasure]);
      currentPage = measure[3];
      firstMeasure = measure[0];
    }
    lastMeasure = measure[0];
  }

  pages.push([currentPage, firstMeasure, lastMeasure]);
  return pages;
}

function getRenderedPagesFromOsmd() {
  const graphicSheet = state.osmd?.GraphicSheet || state.osmd?.graphicSheet;
  const musicPages = graphicSheet?.MusicPages || graphicSheet?.musicPages || [];
  if (!Array.isArray(musicPages) || !musicPages.length) {
    return null;
  }

  const pages = [];

  musicPages.forEach((musicPage, pageIndex) => {
    const systems = musicPage?.MusicSystems || musicPage?.musicSystems || [];
    let renderedMeasureCount = 0;

    systems.forEach((system) => {
      const groupedMeasures = system?.GraphicalMeasures || system?.graphicalMeasures || [];
      const topStaffMeasures = Array.isArray(groupedMeasures) && groupedMeasures.length ? groupedMeasures[0] : [];
      renderedMeasureCount += topStaffMeasures.filter(Boolean).length;
    });

    pages.push({
      pageNumber: pageIndex + 1,
      renderedMeasureCount,
    });
  });

  if (!pages.some((page) => page.renderedMeasureCount > 0)) {
    return null;
  }

  return pages;
}

function getOsmdMeasureList() {
  return (
    state.osmd?.graphic?.measureList ||
    state.osmd?.GraphicSheet?.measureList ||
    state.osmd?.drawer?.graphicalMusicSheet?.measureList ||
    state.osmd?.drawer?.graphicalMusicSheet?.MeasureList ||
    []
  );
}

function getScaleForSvg(svgElement) {
  const rect = svgElement.getBoundingClientRect();
  const viewBox = svgElement.viewBox?.baseVal;
  const baseWidth = viewBox?.width || svgElement.width?.baseVal?.value || rect.width || 1;
  const baseHeight = viewBox?.height || svgElement.height?.baseVal?.value || rect.height || 1;

  return {
    x: rect.width / baseWidth,
    y: rect.height / baseHeight,
  };
}

function extractMeasureBounds(graphicalMeasure) {
  const box =
    graphicalMeasure?.PositionAndShape ||
    graphicalMeasure?.positionAndShape ||
    graphicalMeasure?.boundingBox ||
    null;
  const dataObject = box?.dataObject || box?.DataObject || {};
  const stave = dataObject?.stave || dataObject?.Stave || null;
  const size = box?.size || box?.Size || {};

  const left = stave?.x ?? box?.absolutePosition?.x ?? box?.AbsolutePosition?.x ?? null;
  const top = stave?.y ?? box?.absolutePosition?.y ?? box?.AbsolutePosition?.y ?? null;
  const width = stave?.width ?? size?.width ?? size?.Width ?? null;
  const height =
    size?.height ??
    size?.Height ??
    ((box?.borderBottom ?? box?.BorderBottom ?? 0) - (box?.borderTop ?? box?.BorderTop ?? 0)) ??
    48;

  if ([left, top, width].some((value) => typeof value !== "number" || !Number.isFinite(value))) {
    return null;
  }

  return {
    left,
    top,
    width,
    height: typeof height === "number" && Number.isFinite(height) ? height : 48,
  };
}

function buildMeasureHotspots() {
  state.measureHotspots = [];

  if (!state.bundle) {
    return;
  }

  const measureList = getOsmdMeasureList();
  const pageSvgs = Array.from(dom.scoreSurface.querySelectorAll("svg"));
  if (!measureList.length || !pageSvgs.length) {
    return;
  }

  const canvasRect = dom.scoreCanvas.getBoundingClientRect();

  measureList.forEach((group, measureIndex) => {
    const graphicalMeasure = Array.isArray(group) ? group[0] : group;
    if (!graphicalMeasure || !state.bundle.measures[measureIndex]) {
      return;
    }

    const bounds = extractMeasureBounds(graphicalMeasure);
    if (!bounds) {
      return;
    }

    const pageNumber = state.bundle.measures[measureIndex][3] || 1;
    const svg = pageSvgs[pageNumber - 1] || pageSvgs[0];
    if (!svg) {
      return;
    }

    const scale = getScaleForSvg(svg);
    const svgRect = svg.getBoundingClientRect();
    state.measureHotspots.push({
      measureIndex,
      pageNumber,
      left: svgRect.left - canvasRect.left + dom.scoreCanvas.scrollLeft + bounds.left * scale.x,
      top: svgRect.top - canvasRect.top + dom.scoreCanvas.scrollTop + (bounds.top - 12) * scale.y,
      width: Math.max(24, bounds.width * scale.x),
      height: Math.max(40, bounds.height * scale.y + 24),
    });
  });
}

function applyRenderedPagesToScore(score, renderedPages) {
  if (!score?.parts?.length || !renderedPages?.length) {
    return score;
  }

  score.parts.forEach((bundle) => {
    let cursor = 0;

    renderedPages.forEach((page) => {
      for (let count = 0; count < page.renderedMeasureCount; count += 1) {
        const measure = bundle.measures[cursor];
        if (!measure) {
          break;
        }

        measure[3] = page.pageNumber;

        for (let eventIndex = measure[4]; eventIndex <= measure[5]; eventIndex += 1) {
          if (eventIndex >= 0 && bundle.notes[eventIndex]) {
            bundle.notes[eventIndex][4] = page.pageNumber;
          }
        }

        cursor += 1;
      }
    });

    bundle.pages = buildPagesFromMeasures(bundle.measures);
  });

  return score;
}

function getMarkerIndexForSourcePage(sourcePage) {
  if (!state.bundle) {
    return -1;
  }
  return state.bundle.markers.findIndex((marker) => marker[3] === sourcePage + 1);
}

function getMarkerForSourcePage(sourcePage) {
  const index = getMarkerIndexForSourcePage(sourcePage);
  return index >= 0 ? state.bundle.markers[index] : null;
}

function getCommittedSelectionForSourcePage(sourcePage) {
  const marker = getMarkerForSourcePage(sourcePage);
  const measuresInfo = getMeasuresForSourcePage(sourcePage);
  const selectedMeasureEntry = resolveMeasureEntryForMarker(measuresInfo.entries, marker);
  const measureIndex = selectedMeasureEntry?.measureIndex ?? null;
  const events = getEventsForMeasureIndex(measureIndex);
  const eventIndex = events.some((entry) => entry.eventIndex === marker?.[1])
    ? marker[1]
    : events[events.length - 1]?.eventIndex ?? null;

  return {
    measureIndex,
    eventIndex,
  };
}

function getDraftSelectionForSourcePage(sourcePage) {
  if (state.turnDrafts.has(sourcePage)) {
    return state.turnDrafts.get(sourcePage);
  }

  const committed = getCommittedSelectionForSourcePage(sourcePage);
  if (committed.measureIndex == null || committed.eventIndex == null) {
    return null;
  }

  return committed;
}

function setDraftSelectionForSourcePage(sourcePage, measureIndex, eventIndex) {
  if (measureIndex == null || eventIndex == null) {
    state.turnDrafts.delete(sourcePage);
    return;
  }

  state.turnDrafts.set(sourcePage, {
    measureIndex,
    eventIndex,
  });
}

function clearDraftSelectionForSourcePage(sourcePage) {
  state.turnDrafts.delete(sourcePage);
}

function hasPendingDraftForSourcePage(sourcePage) {
  const marker = getMarkerForSourcePage(sourcePage);
  const draft = state.turnDrafts.get(sourcePage);
  if (!marker || !draft) {
    return false;
  }

  const committed = getCommittedSelectionForSourcePage(sourcePage);
  return (
    draft.measureIndex !== committed.measureIndex ||
    draft.eventIndex !== committed.eventIndex
  );
}

function resolveMeasureEntryForMarker(measureEntries, marker) {
  if (!marker || !measureEntries.length) {
    return null;
  }

  return (
    measureEntries.find((entry) => marker[0] >= entry.measure[1] && marker[0] <= entry.measure[2]) ||
    measureEntries.find((entry) => entry.measure[0] === marker[2]) ||
    (!hasDisplayNoteheads()
      ? measureEntries.find((entry) => marker[1] >= entry.measure[4] && marker[1] <= entry.measure[5])
      : null) ||
    measureEntries[measureEntries.length - 1]
  );
}

function createDefaultMarkerForSourcePage(sourcePage) {
  const measuresInfo = getMeasuresForSourcePage(sourcePage);
  if (!measuresInfo.entries.length) {
    return null;
  }

  const measureEntry = measuresInfo.entries[measuresInfo.entries.length - 1];
  const events = getEventsForMeasureIndex(measureEntry.measureIndex);
  if (!events.length) {
    return null;
  }

  const fallback = events[events.length - 1];
  return [fallback.note[0], fallback.eventIndex, measureEntry.measure[0], sourcePage + 1];
}

function getDefaultDraftSelectionForSourcePage(sourcePage) {
  const measuresInfo = getMeasuresForSourcePage(sourcePage);
  if (!measuresInfo.entries.length) {
    return null;
  }

  const measureEntry = measuresInfo.entries[measuresInfo.entries.length - 1];
  const events = getEventsForMeasureIndex(measureEntry.measureIndex);
  const fallback = events[events.length - 1];
  if (!fallback) {
    return null;
  }

  return {
    measureIndex: measureEntry.measureIndex,
    eventIndex: fallback.eventIndex,
  };
}

function updateMarkerForSourcePage(sourcePage, measureIndex, eventIndex) {
  if (!state.bundle) {
    return;
  }

  const marker = getMarkerForSourcePage(sourcePage);
  const measure = state.bundle.measures[measureIndex];
  const selectedEvent = getEventBySelectionIndex(eventIndex);
  if (!marker || !measure || !selectedEvent) {
    return;
  }

  marker[0] = selectedEvent.note[0];
  marker[1] = eventIndex;
  marker[2] = measure[0];

  sortMarkers();
}

function commitDraftForSourcePage(sourcePage) {
  const marker = getMarkerForSourcePage(sourcePage);
  const draft = getDraftSelectionForSourcePage(sourcePage);
  if (!marker || !draft) {
    return false;
  }

  updateMarkerForSourcePage(sourcePage, draft.measureIndex, draft.eventIndex);
  clearDraftSelectionForSourcePage(sourcePage);
  return true;
}

function commitAllDraftSelections() {
  if (!state.bundle) {
    return;
  }

  for (let sourcePage = 1; sourcePage <= state.pageRowCount; sourcePage += 1) {
    commitDraftForSourcePage(sourcePage);
  }
}

function pickNearestEventInMeasure(measureIndex, ratio) {
  const events = getEventsForMeasureIndex(measureIndex);
  if (!events.length) {
    return null;
  }

  const clampedRatio = Math.min(Math.max(ratio, 0), 1);
  const nearestIndex = Math.round(clampedRatio * Math.max(events.length - 1, 0));
  return events[nearestIndex] ?? events[events.length - 1];
}

function buildNoteHotspotsForMeasure(measureIndex) {
  const measureHotspot = state.measureHotspots.find((entry) => entry.measureIndex === measureIndex);
  const measure = state.bundle?.measures?.[measureIndex];
  const events = getEventsForMeasureIndex(measureIndex);
  if (!measureHotspot || !measure || !events.length) {
    return [];
  }

  const measureStart = measure[1];
  const measureEnd = measure[2];
  const measureDuration = Math.max(1, measureEnd - measureStart);

  return events.map((entry, indexInMeasure) => {
    const eventStart = entry.note[0];
    const eventDuration = Math.max(1, entry.note[1]);
    const ratio = Math.min(Math.max((eventStart - measureStart) / measureDuration, 0), 1);
    const widthRatio = Math.min(Math.max(eventDuration / measureDuration, 0.08), 0.22);
    const left = measureHotspot.left + Math.max(0, ratio * measureHotspot.width - 12);
    const top = measureHotspot.top + 8 + (indexInMeasure % 3) * 24;
    const width = Math.max(28, Math.min(measureHotspot.width * widthRatio, 72));

    return {
      measureIndex,
      eventIndex: entry.eventIndex,
      left,
      top,
      width,
      height: 22,
      label: freqHundredToNoteName(entry.note[2]),
    };
  });
}

function renderScoreOverlay() {
  dom.scoreOverlay.innerHTML = "";

  if (state.visualPickSourcePage == null) {
    dom.scoreOverlay.classList.add("hidden");
    return;
  }

  buildMeasureHotspots();

  if (!state.measureHotspots.length) {
    dom.scoreOverlay.classList.add("hidden");
    return;
  }

  const measuresInfo = getMeasuresForSourcePage(state.visualPickSourcePage);
  const validMeasureIndexes = new Set(measuresInfo.entries.map((entry) => entry.measureIndex));
  const hotspots = state.measureHotspots.filter((hotspot) => validMeasureIndexes.has(hotspot.measureIndex));

  if (!hotspots.length) {
    dom.scoreOverlay.classList.add("hidden");
    return;
  }

  dom.scoreOverlay.classList.remove("hidden");

  if (state.visualPickMeasureIndex != null) {
    const measure = state.bundle.measures[state.visualPickMeasureIndex];
    const noteHotspots = buildNoteHotspotsForMeasure(state.visualPickMeasureIndex);
    if (!noteHotspots.length) {
      state.visualPickMeasureIndex = null;
      renderScoreOverlay();
      return;
    }

    noteHotspots.forEach((hotspot) => {
      const button = document.createElement("button");
      button.type = "button";
      button.className = "score-hotspot score-note-hotspot";
      button.style.left = `${hotspot.left}px`;
      button.style.top = `${hotspot.top}px`;
      button.style.width = `${hotspot.width}px`;
      button.style.height = `${hotspot.height}px`;
      button.textContent = hotspot.label;
      button.title = `Pick ${hotspot.label} in ${getDisplayBarLabel(state.visualPickMeasureIndex)}`;

      button.addEventListener("click", (event) => {
        event.preventDefault();
        event.stopPropagation();

        const sourcePage = state.visualPickSourcePage;
        enableFlipForSourcePage(sourcePage);
        setDraftSelectionForSourcePage(sourcePage, hotspot.measureIndex, hotspot.eventIndex);
        state.visualPickSourcePage = null;
        state.visualPickMeasureIndex = null;
        updateSummary();
        renderTurnList();
        renderScoreOverlay();
        setStatus(`Picked ${hotspot.label} in ${getDisplayBarLabel(hotspot.measureIndex)}. Click Save marker to commit it.`, "ready");
      });

      dom.scoreOverlay.appendChild(button);
    });

    return;
  }

  hotspots.forEach((hotspot) => {
    const measure = state.bundle.measures[hotspot.measureIndex];
    const button = document.createElement("button");
    button.type = "button";
    button.className = "score-hotspot";
    button.style.left = `${hotspot.left}px`;
    button.style.top = `${hotspot.top}px`;
    button.style.width = `${hotspot.width}px`;
    button.style.height = `${hotspot.height}px`;
    button.textContent = `B${getDisplayBarNumber(hotspot.measureIndex)}`;
    button.title = `Pick ${getDisplayBarLabel(hotspot.measureIndex)} on page ${state.visualPickSourcePage}`;

    button.addEventListener("click", (event) => {
      event.preventDefault();
      event.stopPropagation();

      const sourcePage = state.visualPickSourcePage;
      enableFlipForSourcePage(sourcePage);
      state.visualPickMeasureIndex = hotspot.measureIndex;
      renderScoreOverlay();
      setStatus(`${getDisplayBarLabel(hotspot.measureIndex)} selected. Now click a note in that bar.`, "ready");
    });

    dom.scoreOverlay.appendChild(button);
  });
}

function getSuggestedPageRowCount(bundle = state.bundle) {
  if (!bundle) {
    return 0;
  }

  const detectedSourcePages = buildSourcePageRangesFromBundle(bundle).length;
  const highestMarkerSourcePage = bundle.markers.length
    ? Math.max(...bundle.markers.map((marker) => Math.max(0, marker[3] - 1)))
    : 0;

  return Math.min(getMaxSourcePageCount(bundle), Math.max(detectedSourcePages, highestMarkerSourcePage));
}

function ensureAutoMarkers() {
  if (!state.bundle) {
    return;
  }

  for (let sourcePage = 1; sourcePage <= state.pageRowCount; sourcePage += 1) {
    if (state.disabledSourcePages.has(sourcePage) || getMarkerIndexForSourcePage(sourcePage) >= 0) {
      continue;
    }

    const marker = createDefaultMarkerForSourcePage(sourcePage);
    if (marker) {
      state.bundle.markers.push(marker);
    }
  }

  sortMarkers();
}

function realignMarkersToCurrentRanges() {
  if (!state.bundle) {
    return;
  }

  for (let sourcePage = 1; sourcePage <= state.pageRowCount; sourcePage += 1) {
    const marker = getMarkerForSourcePage(sourcePage);
    if (!marker) {
      continue;
    }

    const measuresInfo = getMeasuresForSourcePage(sourcePage);
    if (!measuresInfo.entries.length) {
      continue;
    }

    const measureEntry =
      resolveMeasureEntryForMarker(measuresInfo.entries, marker) ||
      measuresInfo.entries[measuresInfo.entries.length - 1];
    if (!measureEntry) {
      continue;
    }

    const events = getEventsForMeasureIndex(measureEntry.measureIndex);
    if (!events.length) {
      continue;
    }

    const selectedEvent =
      events.find((entry) => entry.eventIndex === marker[1]) ||
      events[events.length - 1];
    updateMarkerForSourcePage(sourcePage, measureEntry.measureIndex, selectedEvent.eventIndex);
  }

  sortMarkers();
}

function refreshPageLayout(seedRanges = state.sourcePageRanges) {
  if (!state.bundle) {
    state.sourcePageRanges = [];
    state.disabledSourcePages = new Set();
    return;
  }

  applySourcePageRangesToBundle(state.bundle, seedRanges);
  pruneDisabledSourcePages();
  ensureAutoMarkers();
  realignMarkersToCurrentRanges();
}

function updateSummary() {
  if (!state.bundle) {
    dom.scoreTitle.textContent = "No score loaded";
    dom.scoreMeta.textContent = "Upload a MusicXML or MXL file to begin.";
    dom.noteCount.textContent = "0";
    dom.measureCount.textContent = "0";
    dom.pageCount.textContent = "0";
    dom.markerCount.textContent = "0";
    dom.exportButton.disabled = true;
    dom.resetMarkersButton.disabled = true;
    dom.addTurnButton.disabled = true;
    return;
  }

  const [partId, partName] = state.bundle.part;
  dom.scoreTitle.textContent = state.bundle.title;
  dom.scoreMeta.textContent = `${partName} (${partId}) • ${formatMs(state.bundle.dur)} runtime`;
  dom.noteCount.textContent = String(state.bundle.notes.length);
  dom.measureCount.textContent = String(state.bundle.measures.length);
  dom.pageCount.textContent = String(state.bundle.pages.length);
  dom.markerCount.textContent = String(state.bundle.markers.length);
  dom.exportButton.disabled = false;
  dom.resetMarkersButton.disabled = false;
  dom.addTurnButton.disabled =
    getAllPitchedMeasures().length === 0 ||
    state.pageRowCount >= getMaxSourcePageCount();
}

function enableFlipForSourcePage(sourcePage) {
  if (!state.bundle || getMarkerIndexForSourcePage(sourcePage) >= 0) {
    return;
  }

  state.disabledSourcePages.delete(sourcePage);

  const marker = createDefaultMarkerForSourcePage(sourcePage);
  if (!marker) {
    return;
  }

  state.bundle.markers.push(marker);
  sortMarkers();
}

function disableFlipForSourcePage(sourcePage) {
  if (!state.bundle) {
    return;
  }

  state.disabledSourcePages.add(sourcePage);
  const markerIndex = getMarkerIndexForSourcePage(sourcePage);
  if (markerIndex >= 0) {
    state.bundle.markers.splice(markerIndex, 1);
  }
  sortMarkers();
}

function renderTurnList() {
  dom.turnList.innerHTML = "";

  if (!state.bundle) {
    dom.turnList.innerHTML = `<div class="empty-list">Upload a score first.</div>`;
    renderScoreOverlay();
    return;
  }

  if (state.pageRowCount === 0) {
    dom.turnHint.textContent =
      "Click Add next page to create the first page break. Each row defines where one source page ends.";
    dom.turnList.innerHTML = `
      <div class="empty-list">
        No page turns configured yet. Click Add next page, then choose the last measure on page 1.
      </div>
    `;
    renderScoreOverlay();
    return;
  }

  ensureAutoMarkers();

  dom.turnHint.textContent =
    "Each editable row ends one source page. The remaining bars are shown automatically as the final page.";

  for (let sourcePage = 1; sourcePage <= state.pageRowCount; sourcePage += 1) {
    const marker = getMarkerForSourcePage(sourcePage);
    const pageRange = getSourcePageRange(sourcePage);
    const rangeOptions = getRangeEndOptionsForSourcePage(sourcePage);
    const measuresInfo = getMeasuresForSourcePage(sourcePage);
    const committedSelection = getCommittedSelectionForSourcePage(sourcePage);
    const draftSelection = getDraftSelectionForSourcePage(sourcePage);
    const selectedMeasureIndex = draftSelection?.measureIndex ?? committedSelection.measureIndex ?? null;
    const events = getEventsForMeasureIndex(selectedMeasureIndex);
    const selectedEventIndex = events.some((entry) => entry.eventIndex === draftSelection?.eventIndex)
      ? draftSelection.eventIndex
      : events.some((entry) => entry.eventIndex === committedSelection.eventIndex)
        ? committedSelection.eventIndex
        : events[events.length - 1]?.eventIndex;
    const hasPendingDraft = hasPendingDraftForSourcePage(sourcePage);

    const card = document.createElement("div");
    card.className = hasPendingDraft ? "turn-card pending" : "turn-card";

    const copy = document.createElement("div");
    copy.className = "turn-copy";
    copy.innerHTML = `
      <strong>Page ${sourcePage}</strong>
      <p class="microcopy">${getSourcePageRangeLabel(sourcePage)}</p>
      <p class="microcopy">Flip to page ${sourcePage + 1}. Choose a measure and note, or use Pick on score.</p>
      <p class="microcopy">${measuresInfo.mode === "page"
        ? "Page ends count every bar, including rests. Flip-note choices come from the playable bars on this page range."
        : "Page breaks were not detected for this row, so measure choices come from the whole score."}</p>
      <p class="turn-status">${!marker
        ? "Flip disabled for this page turn."
        : hasPendingDraft
          ? "Unsaved selection. Click Save marker."
          : "Marker saved."}</p>
    `;

    const actions = document.createElement("div");
    actions.className = "turn-actions";

    const rangeField = document.createElement("label");
    rangeField.className = "turn-field";
    rangeField.innerHTML = `<span>Page end</span>`;
    const rangeSelect = document.createElement("select");
    rangeSelect.disabled = !rangeOptions.length;

    rangeOptions.forEach((entry) => {
      const option = document.createElement("option");
      option.value = String(entry.measureIndex);
      option.textContent = `${getDisplayBarLabel(entry.measureIndex)} ends page ${sourcePage}`;
      option.selected = entry.measureIndex === pageRange?.endMeasureIndex;
      rangeSelect.appendChild(option);
    });

    rangeSelect.addEventListener("change", () => {
      const nextEndMeasureIndex = Number(rangeSelect.value);
      if (!Number.isFinite(nextEndMeasureIndex)) {
        return;
      }

      const nextRanges = state.sourcePageRanges.map((range) => ({ ...range }));
      if (!nextRanges[sourcePage - 1]) {
        return;
      }

      nextRanges[sourcePage - 1].endMeasureIndex = nextEndMeasureIndex;
      state.turnDrafts = new Map();
      state.visualPickSourcePage = null;
      state.visualPickMeasureIndex = null;
      refreshPageLayout(nextRanges);
      updateSummary();
      renderTurnList();
      setStatus(`Page ${sourcePage} now ends at ${getDisplayBarLabel(nextEndMeasureIndex)}.`, "ready");
    });

    rangeField.appendChild(rangeSelect);

    const toggleField = document.createElement("label");
    toggleField.className = "turn-field turn-toggle";
    toggleField.innerHTML = `<span>Flip</span>`;
    const toggleLine = document.createElement("div");
    toggleLine.className = "turn-toggle-line";
    const toggleInput = document.createElement("input");
    toggleInput.type = "checkbox";
    toggleInput.checked = Boolean(marker);
    const toggleText = document.createElement("strong");
    toggleText.textContent = `Flip to page ${sourcePage + 1}`;
    toggleLine.appendChild(toggleInput);
    toggleLine.appendChild(toggleText);
    toggleField.appendChild(toggleLine);

    toggleInput.addEventListener("change", () => {
      if (toggleInput.checked) {
        enableFlipForSourcePage(sourcePage);
        clearDraftSelectionForSourcePage(sourcePage);
      } else {
        disableFlipForSourcePage(sourcePage);
        clearDraftSelectionForSourcePage(sourcePage);
        if (state.visualPickSourcePage === sourcePage) {
          state.visualPickSourcePage = null;
          state.visualPickMeasureIndex = null;
        }
      }
      updateSummary();
      renderTurnList();
    });

    const measureField = document.createElement("label");
    measureField.className = "turn-field";
    measureField.innerHTML = `<span>Trigger bar</span>`;
    const measureSelect = document.createElement("select");
    measureSelect.disabled = !marker || !measuresInfo.entries.length;

    measuresInfo.entries.forEach((entry) => {
      const option = document.createElement("option");
      option.value = String(entry.measureIndex);
      option.textContent = getMeasureLabel(entry, measuresInfo.mode);
      option.selected = entry.measureIndex === selectedMeasureIndex;
      measureSelect.appendChild(option);
    });

    measureSelect.addEventListener("change", () => {
      if (!getMarkerForSourcePage(sourcePage)) {
        return;
      }

      const nextMeasureIndex = Number(measureSelect.value);
      const nextEvents = getEventsForMeasureIndex(nextMeasureIndex);
      const currentDraft = getDraftSelectionForSourcePage(sourcePage);
      const fallback = nextEvents.find((entry) => entry.eventIndex === currentDraft?.eventIndex) || nextEvents[nextEvents.length - 1];
      if (!fallback) {
        return;
      }

      setDraftSelectionForSourcePage(sourcePage, nextMeasureIndex, fallback.eventIndex);
      renderTurnList();
      setStatus(`Measure selected for page ${sourcePage}. Click Save marker to commit it.`, "ready");
    });

    measureField.appendChild(measureSelect);

    const noteField = document.createElement("label");
    noteField.className = "turn-field";
    noteField.innerHTML = `<span>Note</span>`;
    const noteSelect = document.createElement("select");
    noteSelect.disabled = !marker || !events.length;

    events.forEach((entry, indexInMeasure) => {
      const option = document.createElement("option");
      option.value = String(entry.eventIndex);
      option.textContent = getNoteLabel(entry, indexInMeasure);
      option.selected = entry.eventIndex === selectedEventIndex;
      noteSelect.appendChild(option);
    });

    noteSelect.addEventListener("change", () => {
      if (!getMarkerForSourcePage(sourcePage)) {
        return;
      }

      const nextEventIndex = Number(noteSelect.value);
      if (!getEventBySelectionIndex(nextEventIndex)) {
        return;
      }

      setDraftSelectionForSourcePage(sourcePage, selectedMeasureIndex, nextEventIndex);
      renderTurnList();
      setStatus(`Note selected for page ${sourcePage}. Click Save marker to commit it.`, "ready");
    });

    noteField.appendChild(noteSelect);

    const pickButton = document.createElement("button");
    pickButton.type = "button";
    pickButton.className = "mini-button";
    pickButton.textContent = state.visualPickSourcePage === sourcePage ? "Picking..." : "Pick on score";
    pickButton.disabled = !measuresInfo.entries.length;
    pickButton.addEventListener("click", () => {
      if (!getMarkerForSourcePage(sourcePage)) {
        enableFlipForSourcePage(sourcePage);
      }
      const isSameSourcePage = state.visualPickSourcePage === sourcePage;
      state.visualPickSourcePage = isSameSourcePage ? null : sourcePage;
      state.visualPickMeasureIndex = isSameSourcePage ? null : null;
      renderTurnList();
      renderScoreOverlay();
      setStatus(
        state.visualPickSourcePage === sourcePage
          ? `Click a measure, then a note, on the rendered score for page ${sourcePage}.`
          : "Score picking cancelled.",
        "ready"
      );
    });

    const button = document.createElement("button");
    button.type = "button";
    button.className = "mini-button";
    button.textContent = "Default";
    button.disabled = !measuresInfo.entries.length;

    button.addEventListener("click", () => {
      if (!getMarkerForSourcePage(sourcePage)) {
        enableFlipForSourcePage(sourcePage);
      }
      const defaultSelection = getDefaultDraftSelectionForSourcePage(sourcePage);
      if (!defaultSelection) {
        return;
      }
      setDraftSelectionForSourcePage(sourcePage, defaultSelection.measureIndex, defaultSelection.eventIndex);
      renderTurnList();
      setStatus(`Default marker selected for page ${sourcePage}. Click Save marker to commit it.`, "ready");
    });

    const saveButton = document.createElement("button");
    saveButton.type = "button";
    saveButton.className = "mini-button primary";
    saveButton.textContent = hasPendingDraft ? "Save marker" : "Saved";
    saveButton.disabled = !marker || selectedMeasureIndex == null || selectedEventIndex == null || !hasPendingDraft;

    saveButton.addEventListener("click", () => {
      if (!commitDraftForSourcePage(sourcePage)) {
        return;
      }

      updateSummary();
      renderTurnList();
      setStatus(`Saved marker for page ${sourcePage}.`, "ready");
    });

    actions.appendChild(rangeField);
    actions.appendChild(toggleField);
    actions.appendChild(measureField);
    actions.appendChild(noteField);
    actions.appendChild(pickButton);
    actions.appendChild(button);
    actions.appendChild(saveButton);

    card.appendChild(copy);
    card.appendChild(actions);
    dom.turnList.appendChild(card);
  }

  const finalPageRange = getFinalPageRange();
  if (finalPageRange) {
    const finalPageCard = document.createElement("div");
    finalPageCard.className = "turn-card";

    const finalCopy = document.createElement("div");
    finalCopy.className = "turn-copy";
    finalCopy.innerHTML = `
      <strong>Page ${state.pageRowCount + 1} (Final page)</strong>
      <p class="microcopy">${getRangeLabel(finalPageRange)}</p>
      <p class="microcopy">These remaining bars stay on the last page.</p>
      <p class="turn-status">No flip marker needed after the final page.</p>
    `;

    finalPageCard.appendChild(finalCopy);
    dom.turnList.appendChild(finalPageCard);
  }

  renderScoreOverlay();
}

function addPageRow() {
  if (!state.bundle) {
    return;
  }

  if (state.pageRowCount >= getMaxSourcePageCount()) {
    setStatus("Every page needs at least one measure, so no more page rows can be added.", "error");
    return;
  }

  state.pageRowCount += 1;
  refreshPageLayout();
  updateSummary();
  renderTurnList();
  setStatus(`Added page ${state.pageRowCount}. Choose where this page ends.`, "ready");
}

function applySelectedPart(partId) {
  const template = state.bundleTemplates.get(partId);
  if (!template) {
    return;
  }

  state.selectedPartId = partId;
  state.bundle = cloneBundle(template);
  state.pageRowCount = getSuggestedPageRowCount(state.bundle);
  state.sourcePageRanges = buildSourcePageRangesFromBundle(state.bundle);
  state.disabledSourcePages = new Set();
  state.visualPickSourcePage = null;
  state.visualPickMeasureIndex = null;
  state.turnDrafts = new Map();
  refreshPageLayout(state.sourcePageRanges);
  buildMeasureHotspots();
  updateSummary();
  renderTurnList();
  setStatus("Set each page end, then choose the measure and note that should trigger the flip.", "ready");
}

function populatePartSelect(parts) {
  dom.partSelect.innerHTML = "";

  if (!parts.length) {
    state.bundle = null;
    state.selectedPartId = null;
    state.pageRowCount = 0;
    state.sourcePageRanges = [];
    state.disabledSourcePages = new Set();
    state.visualPickSourcePage = null;
    state.visualPickMeasureIndex = null;
    state.turnDrafts = new Map();
    updateSummary();
    renderTurnList();
  }

  for (const bundle of parts) {
    const option = document.createElement("option");
    option.value = bundle.part[0];
    option.textContent = `${bundle.part[1]} (${bundle.part[0]})`;
    dom.partSelect.appendChild(option);
    state.bundleTemplates.set(bundle.part[0], bundle);
  }

  dom.partSelect.disabled = parts.length === 0;
  if (parts.length > 0) {
    dom.partSelect.value = parts[0].part[0];
    applySelectedPart(parts[0].part[0]);
  }
}

async function renderScore(file) {
  dom.scoreSurface.innerHTML = "";
  dom.scoreOverlay.innerHTML = "";
  dom.scoreOverlay.classList.add("hidden");
  state.measureHotspots = [];

  if (!window.opensheetmusicdisplay) {
    dom.scoreSurface.innerHTML = `
      <div class="empty-state">
        <strong>Score rendering library unavailable.</strong>
        <p>The upload and page-turn editor still work; the CDN script did not load.</p>
      </div>
    `;
    return null;
  }

  if (!state.osmd) {
    state.osmd = new window.opensheetmusicdisplay.OpenSheetMusicDisplay(dom.scoreSurface, {
      autoResize: true,
      backend: "svg",
      drawTitle: true,
    });
  }

  try {
    const lowerName = file.name.toLowerCase();
    if (lowerName.endsWith(".mxl")) {
      const buffer = await file.arrayBuffer();
      await state.osmd.load(new Uint8Array(buffer));
    } else {
      const xmlText = await file.text();
      await state.osmd.load(xmlText);
    }
    state.osmd.render();
  } catch (error) {
    dom.scoreSurface.innerHTML = `
      <div class="empty-state">
        <strong>Could not render this score in the browser.</strong>
        <p>${error?.message || "The MusicXML parsed, but the score renderer failed."}</p>
      </div>
    `;
    state.measureHotspots = [];
    renderScoreOverlay();
    return null;
  }

  return getRenderedPagesFromOsmd();
}

async function parseScore(file) {
  const formData = new FormData();
  formData.append("file", file);

  const response = await fetch("/api/parse", {
    method: "POST",
    body: formData,
  });

  const payload = await response.json();
  if (!response.ok) {
    throw new Error(payload.error || "Parsing failed.");
  }

  return payload;
}

async function handleUpload() {
  const [file] = dom.fileInput.files || [];
  if (!file) {
    return;
  }

  state.file = file;
  state.bundle = null;
  state.selectedPartId = null;
  state.pageRowCount = 0;
  state.sourcePageRanges = [];
  state.disabledSourcePages = new Set();
  state.visualPickSourcePage = null;
  state.visualPickMeasureIndex = null;
  state.turnDrafts = new Map();
  state.bundleTemplates.clear();
  dom.partSelect.innerHTML = "";
  dom.downloadLink.classList.add("hidden");
  dom.downloadLink.href = "";
  dom.exportPath.textContent = "Nothing exported yet";
  updateSummary();
  renderTurnList();
  setStatus("Parsing MusicXML and rendering the score…", "busy");

  try {
    const [score, renderedPages] = await Promise.all([parseScore(file), renderScore(file)]);
    state.parsedScore = applyRenderedPagesToScore(score, renderedPages);
    state.bundleTemplates.clear();
    populatePartSelect(state.parsedScore.parts || []);
  } catch (error) {
    state.bundle = null;
    updateSummary();
    renderTurnList();
    renderScoreOverlay();
    setStatus(error.message || "Upload failed.", "error");
    return;
  }

  setStatus("Score ready. Add page rows if needed, set each page end, then save the flip markers you want.", "ready");
}

async function handleExport() {
  if (!state.bundle) {
    return;
  }

  commitAllDraftSelections();
  renderTurnList();
  setStatus("Exporting compact runtime JSON…", "busy");
  const exportMarkers = state.bundle.markers.map((marker) => [
    marker[0],
    getCompactEventIndexForTime(marker[0]),
    marker[2],
    marker[3],
  ]);
  const exportBundle = {
    v: state.bundle.v,
    title: state.bundle.title,
    part: state.bundle.part,
    dur: state.bundle.dur,
    notes: state.bundle.notes,
    measures: state.bundle.measures,
    pages: state.bundle.pages,
    markers: exportMarkers,
  };
  const response = await fetch("/api/export", {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
    },
    body: JSON.stringify({ bundle: exportBundle }),
  });

  const payload = await response.json();
  if (!response.ok) {
    setStatus(payload.error || "Export failed.", "error");
    return;
  }

  dom.exportPath.textContent = payload.encoreScoreFile || payload.file;
  dom.downloadLink.href = payload.url;
  dom.downloadLink.classList.remove("hidden");
  setStatus("Compact runtime bundle exported and synced to Encore successfully.", "ready");
}

function resetMarkers() {
  if (!state.selectedPartId) {
    return;
  }

  applySelectedPart(state.selectedPartId);
  setStatus("Page rows and markers reset to the current score defaults.", "ready");
}

dom.fileInput.addEventListener("change", handleUpload);
dom.exportButton.addEventListener("click", handleExport);
dom.resetMarkersButton.addEventListener("click", resetMarkers);
dom.addTurnButton.addEventListener("click", addPageRow);
dom.partSelect.addEventListener("change", () => applySelectedPart(dom.partSelect.value));
dom.scoreCanvas.addEventListener("scroll", () => {
  if (state.visualPickSourcePage != null) {
    renderScoreOverlay();
  }
});
window.addEventListener("resize", () => {
  if (state.visualPickSourcePage != null) {
    renderScoreOverlay();
  }
});

updateSummary();
renderTurnList();
