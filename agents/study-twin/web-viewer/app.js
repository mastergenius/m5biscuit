const state = {
  packs: [],
  pack: null,
  concepts: new Map(),
  rubrics: new Map(),
  episodes: [],
  currentIndex: 0,
  revealed: false,
  selectedChoiceId: null,
  packFilter: "",
  shownAt: performance.now(),
  serviceWorkerReady: false,
  flushing: false,
  sessionStats: {
    shown: 0,
    revealed: 0,
    answered: 0,
    rated: 0,
  },
};

const els = {
  packCount: document.querySelector("#pack-count"),
  packList: document.querySelector("#pack-list"),
  packFilter: document.querySelector("#pack-filter"),
  refreshPacks: document.querySelector("#refresh-packs"),
  packMeta: document.querySelector("#pack-meta"),
  packDescription: document.querySelector("#pack-description"),
  episodeTitle: document.querySelector("#episode-title"),
  episodeType: document.querySelector("#episode-type"),
  episodeProgress: document.querySelector("#episode-progress"),
  promptText: document.querySelector("#prompt-text"),
  choiceList: document.querySelector("#choice-list"),
  openResponse: document.querySelector("#open-response"),
  revealPane: document.querySelector("#reveal-pane"),
  revealText: document.querySelector("#reveal-text"),
  rubricLabel: document.querySelector("#rubric-label"),
  rubric: document.querySelector("#rubric"),
  revealButton: document.querySelector("#reveal-button"),
  ratingActions: document.querySelector("#rating-actions"),
  prevEpisode: document.querySelector("#prev-episode"),
  nextEpisode: document.querySelector("#next-episode"),
  runtimeStatusText: document.querySelector("#runtime-status-text"),
  sessionSummary: document.querySelector("#session-summary"),
  syncEvents: document.querySelector("#sync-events"),
  exportEvents: document.querySelector("#export-events"),
};

const REVIEW_QUEUE_KEY = "biscuit.study-viewer.reviewQueue.v1";

const ratingLabels = {
  again: ["Again", "Не получилось удержать ответ"],
  hard: ["Hard", "Ответ есть, но с высокой нагрузкой"],
  good: ["Good", "Рабочее воспроизведение без уверенного автоматизма"],
  easy: ["Easy", "Ответ стабилен и дается легко"],
  confused: ["Confused", "Непонятна рамка или критерий"],
  skipped: ["Skip", "Пропустить без оценки"],
  known: ["Знал", "Это уже известно; можно усложнять"],
  relevant: ["В тему", "Сейчас применимо к реальной ситуации"],
  deeper: ["Глубже", "Хочу отдельный разбор или следующий кейс"],
  unclear: ["Мутно", "Разбор не снял непонимание"],
};

const ratingSignal = {
  again: { confidence: 1, effort: 5 },
  hard: { confidence: 2, effort: 4 },
  good: { confidence: 4, effort: 2 },
  easy: { confidence: 5, effort: 1 },
  confused: { confidence: 1, effort: 4 },
  skipped: { confidence: 0, effort: 0 },
  known: { confidence: 5, effort: 1 },
  relevant: { confidence: 4, effort: 2 },
  deeper: { confidence: 4, effort: 3 },
  unclear: { confidence: 1, effort: 4 },
};

function text(value, fallback = "") {
  return typeof value === "string" ? value : fallback;
}

async function api(path, options = {}) {
  const response = await fetch(path, {
    headers: { "Content-Type": "application/json" },
    ...options,
  });
  if (!response.ok) {
    const body = await response.text();
    throw new Error(`${response.status} ${body || response.statusText}`);
  }
  return response.json();
}

function readReviewQueue() {
  try {
    const value = JSON.parse(localStorage.getItem(REVIEW_QUEUE_KEY) || "[]");
    return Array.isArray(value) ? value : [];
  } catch (error) {
    return [];
  }
}

function writeReviewQueue(queue) {
  localStorage.setItem(REVIEW_QUEUE_KEY, JSON.stringify(queue));
}

function queueReviewEvent(payload) {
  const queue = readReviewQueue();
  queue.push({ ...payload, queued_at: new Date().toISOString() });
  writeReviewQueue(queue);
}

function updateRuntimeStatus(message = "") {
  const queued = readReviewQueue().length;
  const network = navigator.onLine ? "online" : "offline";
  const sw = state.serviceWorkerReady ? "cached" : "no offline shell";
  const suffix = queued === 1 ? "1 queued event" : `${queued} queued events`;
  els.runtimeStatusText.textContent = message || `${network} · ${sw} · ${suffix}`;
  els.syncEvents.disabled = queued === 0 || state.flushing;
  updateSessionSummary();
}

function updateSessionSummary() {
  const { shown, revealed, answered, rated } = state.sessionStats;
  const total = shown + revealed + answered + rated;
  els.sessionSummary.textContent = `${total} events · ${shown} shown · ${answered} answered · ${rated} rated`;
}

function recordSessionEvent(action) {
  if (action === "shown") {
    state.sessionStats.shown += 1;
  } else if (action === "revealed") {
    state.sessionStats.revealed += 1;
  } else if (action === "answered") {
    state.sessionStats.answered += 1;
  } else {
    state.sessionStats.rated += 1;
  }
  updateSessionSummary();
}

async function postReviewEvent(payload) {
  await api("/api/review", {
    method: "POST",
    body: JSON.stringify(payload),
  });
}

async function flushReviewQueue() {
  if (state.flushing) return;
  const queue = readReviewQueue();
  if (queue.length === 0) {
    updateRuntimeStatus();
    return;
  }
  state.flushing = true;
  updateRuntimeStatus("syncing queued events");
  const unsent = [];
  for (let index = 0; index < queue.length; index += 1) {
    const event = queue[index];
    try {
      await postReviewEvent(event);
    } catch (error) {
      unsent.push(event);
      unsent.push(...queue.slice(index + 1));
      break;
    }
  }
  writeReviewQueue(unsent);
  state.flushing = false;
  updateRuntimeStatus(unsent.length === 0 ? "sync complete" : "sync incomplete, still offline");
}

function parseJsonl(value) {
  if (!value) return [];
  return value
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter(Boolean)
    .map((line) => JSON.parse(line));
}

function currentEpisode() {
  return state.episodes[state.currentIndex] || null;
}

function storageKey(packId) {
  return `biscuit.study-viewer.${packId}.index`;
}

function setProgress(index) {
  if (!state.pack) return;
  state.currentIndex = Math.max(0, Math.min(index, state.episodes.length - 1));
  localStorage.setItem(storageKey(state.pack.manifest.pack_id), String(state.currentIndex));
}

async function logEvent(action, extra = {}) {
  const episode = currentEpisode();
  if (!state.pack || !episode) return;
  const payload = {
    pack_id: state.pack.manifest.pack_id,
    pack_revision: state.pack.manifest.revision,
    episode_id: episode.id,
    concept_ids: Array.isArray(episode.concept_ids) ? episode.concept_ids : [],
    action,
    session_ms: Math.max(0, Math.round(performance.now())),
    latency_ms: Math.max(0, Math.round(performance.now() - state.shownAt)),
    ...extra,
  };
  try {
    await postReviewEvent(payload);
  } catch (error) {
    queueReviewEvent(payload);
  } finally {
    recordSessionEvent(action);
    updateRuntimeStatus();
  }
}

async function exportReviewLog() {
  const response = await fetch("/api/reviews/export");
  if (!response.ok) {
    throw new Error(`${response.status} ${await response.text()}`);
  }
  const blob = await response.blob();
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  link.href = url;
  link.download = "web_reviews_000001.jsonl";
  document.body.append(link);
  link.click();
  link.remove();
  URL.revokeObjectURL(url);
  updateRuntimeStatus("review log exported");
}

function renderPackList() {
  const query = state.packFilter.trim().toLowerCase();
  const visiblePacks = query
    ? state.packs.filter((pack) => {
        const haystack = `${pack.id} ${pack.title} ${pack.description}`.toLowerCase();
        return haystack.includes(query);
      })
    : state.packs;
  els.packCount.textContent = query ? `${visiblePacks.length} / ${state.packs.length} packs` : `${state.packs.length} packs`;
  els.packList.replaceChildren(
    ...visiblePacks.map((pack) => {
      const button = document.createElement("button");
      button.className = "pack-button";
      button.type = "button";
      button.setAttribute("role", "listitem");
      button.setAttribute("aria-current", state.pack?.manifest.pack_id === pack.id ? "true" : "false");
      button.innerHTML = `
        <span class="pack-title"></span>
        <span class="pack-detail"></span>
      `;
      button.querySelector(".pack-title").textContent = pack.title || pack.id;
      button.querySelector(".pack-detail").textContent = `${pack.episode_count || 0} episodes · ${pack.revision || "no revision"}`;
      button.addEventListener("click", () => loadPack(pack.id));
      return button;
    }),
  );
}

function renderChoices(episode) {
  const choices = episode.response?.choices || [];
  const showChoices = choices.length > 0 && episode.response?.mode === "choice";
  els.choiceList.hidden = !showChoices;
  els.choiceList.replaceChildren();
  if (!showChoices) return;

  for (const choice of choices) {
    const button = document.createElement("button");
    button.className = "choice-button";
    button.type = "button";
    button.textContent = choice.label || choice.id;
    button.setAttribute("aria-pressed", state.selectedChoiceId === choice.id ? "true" : "false");
    button.addEventListener("click", async () => {
      state.selectedChoiceId = choice.id;
      await logEvent("answered", { response: { choice_id: choice.id } });
      state.revealed = true;
      renderEpisode();
    });
    els.choiceList.append(button);
  }
}

function renderRubric(episode) {
  const rubric = state.rubrics.get(episode.rubric_id);
  els.rubricLabel.textContent = rubric ? rubric.kind : "";
  els.rubric.replaceChildren();
  if (!rubric?.criteria) return;
  for (const item of rubric.criteria) {
    const div = document.createElement("div");
    div.className = "criterion";
    div.textContent = item.text;
    els.rubric.append(div);
  }
}

function visibleActions(episode) {
  const actions = episode.response?.actions || [];
  return actions.filter((action) => action !== "shown" && action !== "revealed" && action !== "answered");
}

function actionMeta(episode, actionId) {
  const rubric = state.rubrics.get(episode.rubric_id);
  const rubricAction = rubric?.actions?.find((action) => action.id === actionId);
  const [fallbackLabel, fallbackMeaning] = ratingLabels[actionId] || [actionId, actionId];
  return {
    label: rubricAction?.label || fallbackLabel,
    meaning: rubricAction?.meaning || fallbackMeaning,
  };
}

function renderActions(episode) {
  els.ratingActions.replaceChildren();
  const actions = visibleActions(episode);
  for (const action of actions) {
    const { label, meaning } = actionMeta(episode, action);
    const button = document.createElement("button");
    button.className = "rating-button";
    button.type = "button";
    button.dataset.action = action;
    button.textContent = label;
    button.title = meaning;
    button.disabled = !state.revealed && action !== "skipped";
    button.addEventListener("click", async () => {
      const extra = { ...ratingSignal[action] };
      const note = els.openResponse.hidden ? "" : els.openResponse.value.trim();
      if (note) extra.response = { note };
      await logEvent(action, extra);
      goNext();
    });
    els.ratingActions.append(button);
  }
}

function renderEpisode() {
  const episode = currentEpisode();
  const hasPack = Boolean(state.pack && episode);
  els.prevEpisode.disabled = !hasPack || state.currentIndex === 0;
  els.nextEpisode.disabled = !hasPack || state.currentIndex >= state.episodes.length - 1;
  els.revealButton.disabled = !hasPack || state.revealed;

  if (!hasPack) {
    els.packMeta.textContent = "No pack selected";
    els.packDescription.textContent = "";
    els.episodeTitle.textContent = "Select a StudyPack";
    els.episodeType.textContent = "Runtime";
    els.episodeProgress.textContent = "0 / 0";
    return;
  }

  els.packMeta.textContent = `${state.pack.manifest.title} · ${state.pack.manifest.revision}`;
  els.packDescription.textContent = text(state.pack.manifest.description);
  els.episodeTitle.textContent = episode.title || episode.id;
  els.episodeType.textContent = `${episode.type || "episode"} · ${episode.response?.mode || "none"}`;
  els.episodeProgress.textContent = `${state.currentIndex + 1} / ${state.episodes.length}`;
  els.promptText.textContent = text(episode.prompt);
  els.revealPane.classList.toggle("is-hidden", !state.revealed);
  els.revealText.textContent = state.revealed ? text(episode.reveal, "No reveal text") : "Reveal appears here after the first attempt.";
  els.openResponse.hidden = episode.response?.mode !== "open_ended";
  renderChoices(episode);
  renderRubric(episode);
  renderActions(episode);
  renderPackList();
}

async function showEpisode(index) {
  setProgress(index);
  state.revealed = false;
  state.selectedChoiceId = null;
  state.shownAt = performance.now();
  els.openResponse.value = "";
  renderEpisode();
  await logEvent("shown");
}

function nextIndexFromEpisode(episode) {
  if (Array.isArray(episode.next) && episode.next.length > 0) {
    const nextId = episode.next[0];
    const found = state.episodes.findIndex((candidate) => candidate.id === nextId);
    if (found >= 0) return found;
  }
  return Math.min(state.currentIndex + 1, state.episodes.length - 1);
}

async function goNext() {
  const episode = currentEpisode();
  if (!episode) return;
  await showEpisode(nextIndexFromEpisode(episode));
}

async function loadPack(packId) {
  const pack = await api(`/api/packs/${encodeURIComponent(packId)}`);
  state.pack = pack;
  state.concepts = new Map(parseJsonl(pack.concepts_jsonl).map((concept) => [concept.id, concept]));
  state.rubrics = new Map(parseJsonl(pack.rubrics_jsonl).map((rubric) => [rubric.id, rubric]));
  state.episodes = parseJsonl(pack.episodes_jsonl);
  const saved = Number(localStorage.getItem(storageKey(pack.manifest.pack_id)) || "0");
  await showEpisode(Number.isFinite(saved) ? saved : 0);
}

async function loadPacks() {
  els.packCount.textContent = "Loading packs";
  const value = await api("/api/packs");
  state.packs = Array.isArray(value.packs) ? value.packs : [];
  renderPackList();
  if (!state.pack && state.packs.length > 0) {
    await loadPack(state.packs[0].id);
  }
  updateRuntimeStatus();
}

async function registerServiceWorker() {
  if (!("serviceWorker" in navigator)) {
    updateRuntimeStatus("service worker unavailable in this browser");
    return;
  }
  try {
    await navigator.serviceWorker.register("/sw.js");
    await navigator.serviceWorker.ready;
    state.serviceWorkerReady = true;
  } catch (error) {
    state.serviceWorkerReady = false;
  } finally {
    updateRuntimeStatus();
  }
}

els.refreshPacks.addEventListener("click", loadPacks);
els.syncEvents.addEventListener("click", flushReviewQueue);
els.exportEvents.addEventListener("click", () => {
  exportReviewLog().catch((error) => updateRuntimeStatus(`export failed: ${error.message}`));
});
els.packFilter.addEventListener("input", () => {
  state.packFilter = els.packFilter.value;
  renderPackList();
});
els.prevEpisode.addEventListener("click", () => showEpisode(state.currentIndex - 1));
els.nextEpisode.addEventListener("click", () => showEpisode(state.currentIndex + 1));
els.revealButton.addEventListener("click", async () => {
  state.revealed = true;
  await logEvent("revealed");
  renderEpisode();
});

document.addEventListener("keydown", (event) => {
  const target = event.target;
  const isTextInput = target instanceof HTMLInputElement || target instanceof HTMLTextAreaElement;
  if (event.key === "/" && !isTextInput) {
    event.preventDefault();
    els.packFilter.focus();
    return;
  }
  if (isTextInput) return;

  if (event.key === "ArrowLeft") {
    event.preventDefault();
    showEpisode(state.currentIndex - 1);
  } else if (event.key === "ArrowRight") {
    event.preventDefault();
    showEpisode(state.currentIndex + 1);
  } else if (event.key.toLowerCase() === "r") {
    event.preventDefault();
    els.revealButton.click();
  } else if (/^[1-5]$/.test(event.key)) {
    const buttons = Array.from(els.ratingActions.querySelectorAll("button:not(:disabled)"));
    const index = Number(event.key) - 1;
    if (buttons[index]) {
      event.preventDefault();
      buttons[index].click();
    }
  }
});

window.addEventListener("online", flushReviewQueue);
window.addEventListener("offline", () => updateRuntimeStatus());
document.addEventListener("visibilitychange", () => {
  if (document.visibilityState === "visible") {
    flushReviewQueue();
  }
});

registerServiceWorker()
  .then(() => loadPacks())
  .then(() => flushReviewQueue())
  .catch((error) => {
    els.packCount.textContent = "Load failed";
    els.episodeTitle.textContent = "Could not load StudyPacks";
    els.promptText.textContent = error.message;
    updateRuntimeStatus("offline cache is empty or server is unavailable");
  });
