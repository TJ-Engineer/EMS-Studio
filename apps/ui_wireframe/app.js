// SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
// SPDX-License-Identifier: MIT

"use strict";

const selectionData = {
  "cad.document": {
    name: "Feed Enclosure", type: "DOCUMENT", icon: "F", id: "document · rev-18",
    groups: [
      ["Document state", [["Semantic head", "revision 18"], ["Evaluated geometry", "revision 18", "good"], ["Features", "5"], ["Bodies", "1"]]],
      ["Recovery", [["Checkpoint", "Current", "good"], ["Journal commands", "18"], ["Last evaluation", "42 ms"]]]
    ]
  },
  "cad.parameters": {
    name: "Parameters", type: "TABLE", icon: "ƒ", id: "parameters · 6 values",
    groups: [
      ["Design parameters", [["body_width", "84.00 mm", "input"], ["body_depth", "56.00 mm", "input"], ["wall_height", "24.00 mm", "input"], ["fillet_radius", "4.00 mm", "input"], ["port_diameter", "12.00 mm", "input"]]],
      ["Evaluation", [["Dependents", "4 features"], ["State", "Up to date", "good"]]]
    ]
  },
  "cad.origin": {
    name: "Origin", type: "REFERENCE", icon: "⊹", id: "datum · global-origin",
    groups: [["Coordinate system", [["System", "Right handed"], ["X axis", "1, 0, 0"], ["Y axis", "0, 1, 0"], ["Z axis", "0, 0, 1"], ["Units", "millimeters"]]]]
  },
  "cad.body": {
    name: "Housing", type: "SOLID BODY", icon: "◆", id: "body · exact-brep-01",
    groups: [["Geometry", [["Kernel", "OCCT adapter"], ["Faces", "24"], ["Edges", "48"], ["Volume", "92,844 mm³"]]], ["Identity", [["Reconciled", "24 / 24", "good"], ["Ambiguous", "0", "good"]]]]
  },
  "cad.sketch": {
    name: "Base Sketch", type: "SKETCH", icon: "⌗", id: "sketch · base-profile",
    groups: [["Constraint state", [["Classification", "Fully constrained", "good"], ["Entities", "8"], ["Constraints", "14"], ["Remaining DOF", "0", "good"]]], ["Support", [["Plane", "XY Plane"], ["Profile area", "4,704 mm²"]]]]
  },
  "cad.extrude": {
    name: "Base Extrude", type: "FEATURE", icon: "▰", id: "feature · extrude-01",
    groups: [["Operation", [["Profile", "Base Sketch"], ["Distance", "24.00 mm", "input"], ["Direction", "Normal"], ["Boolean", "New body", "select"]]], ["Evaluation", [["Status", "Succeeded", "good"], ["Generated faces", "10"], ["Duration", "9 ms"]]]]
  },
  "cad.fillet": {
    name: "Edge Fillet", type: "FEATURE", icon: "⌒", id: "feature · fillet-01",
    groups: [["Definition", [["Edges", "4 selected"], ["Radius", "4.00 mm", "input"], ["Continuity", "Tangent (G1)", "select"]]], ["Topology", [["Status", "Stable", "good"], ["Generated faces", "4"], ["Reconciled IDs", "4 / 4", "good"], ["Confidence", "1.00"]]], ["Evaluation", [["State", "Succeeded", "good"], ["Duration", "13 ms"]]]]
  },
  "cad.face_sketch": {
    name: "Port Sketch", type: "SKETCH", icon: "⌗", id: "sketch · port-profile",
    groups: [["Constraint state", [["Classification", "Fully constrained", "good"], ["Entities", "1 circle"], ["Diameter", "12.00 mm", "input"], ["Remaining DOF", "0", "good"]]], ["Attachment", [["Support", "FilletResult/Face:8"], ["Identity status", "Resolved", "good"], ["Offset", "0.00 mm"]]]]
  },
  "cad.cut": {
    name: "Port Cut", type: "FEATURE", icon: "◫", id: "feature · cut-01",
    groups: [["Operation", [["Profile", "Port Sketch"], ["Extent", "Through all", "select"], ["Target", "Housing"]]], ["Evaluation", [["State", "Succeeded", "good"], ["Result volume", "90,130 mm³"], ["Duration", "18 ms"]]]]
  },
  "cad.material": {
    name: "Aluminum 6061", type: "MATERIAL", icon: "◩", id: "material · al-6061-t6",
    groups: [["Physical", [["Density", "2.70 g/cm³"], ["Mass", "243.4 g"], ["Appearance", "Satin aluminum"]]], ["Source", [["Library", "OEP materials"], ["Revision", "2026.1"]]]]
  },
  "em.study": {
    name: "2.45 GHz Study", type: "EM STUDY", icon: "λ", id: "study · antenna-01",
    groups: [["Frequency", [["Center", "2.45 GHz", "input"], ["Start", "2.20 GHz", "input"], ["Stop", "2.70 GHz", "input"]]], ["Authority", [["ASIR", "0.1"], ["CAD revision", "18", "good"], ["Fingerprint", "9a47…e20c"]]]]
  },
  "em.geometry": {
    name: "Geometry", type: "GEOMETRY", icon: "◆", id: "asir · geometry",
    groups: [["Authoritative source", [["Document", "Feed Enclosure"], ["Evaluated revision", "18", "good"], ["Geometry hash", "4d0a…3f81"], ["Role entities", "7"]]], ["Preparation", [["Primitive regions", "3"], ["Unresolved identities", "0", "good"]]]]
  },
  "em.materials": {
    name: "Materials", type: "COLLECTION", icon: "◩", id: "asir · materials",
    groups: [["Assignments", [["Copper", "2 surfaces"], ["FR-4", "1 volume"], ["Free space", "Background"]]], ["Validation", [["Frequency coverage", "2.2–2.7 GHz", "good"], ["Conflicts", "0", "good"]]]]
  },
  "em.copper": {
    name: "Copper", type: "MATERIAL", icon: "◩", id: "material · copper",
    groups: [["Electromagnetic", [["Model", "PEC preview", "select"], ["Conductivity", "5.80e7 S/m"], ["Relative μ", "1.0"]]], ["Assignment", [["Patch", "Surface role"], ["Ground", "Surface role"]]]]
  },
  "em.fr4": {
    name: "FR-4", type: "MATERIAL", icon: "◩", id: "material · fr4",
    groups: [["Electromagnetic", [["Relative ε", "4.30", "input"], ["Loss tangent", "0.018", "input"], ["Relative μ", "1.0"]]], ["Assignment", [["Substrate", "Volume role"], ["Thickness", "1.60 mm"]]]]
  },
  "em.port": {
    name: "Feed Port", type: "PORT", icon: "P1", id: "port · coax-feed-01",
    groups: [["Definition", [["Type", "Lumped port", "select"], ["Impedance", "50.00 Ω", "input"], ["Orientation", "+Z inward"], ["Reference plane", "z = 0.00 mm"]]], ["Contract", [["Normalization", "RMS power wave"], ["Phase anchor", "+X conductor edge"], ["Status", "Resolved", "good"]]], ["Excitation", [["Waveform", "Gaussian pulse", "select"], ["Amplitude", "1.00 √W", "input"]]]]
  },
  "em.boundaries": {
    name: "Open Boundaries", type: "BOUNDARY", icon: "▧", id: "boundary · domain-faces",
    groups: [["Domain faces", [["−X / +X", "First-order Mur"], ["−Y / +Y", "First-order Mur"], ["−Z / +Z", "First-order Mur"]]], ["Clearance", [["Minimum", "λ₀ / 4"], ["Status", "Valid", "good"]]]]
  },
  "em.grid": {
    name: "Cartesian Grid", type: "MESH", icon: "▦", id: "grid · preview-01",
    groups: [["Resolution", [["Dimensions", "96 × 72 × 28"], ["Total cells", "193,536"], ["Minimum step", "0.80 mm", "input"], ["Courant factor", "0.99", "input"]]], ["Quality", [["Narrowest feature", "1.5 cells", "warning"], ["Volume error", "0.00%", "good"], ["Staircasing", "Review", "warning"]]], ["Traceability", [["Material cells", "24,192"], ["Port faces", "32"], ["Unresolved roles", "0", "good"]]]]
  },
  "em.monitors": {
    name: "Monitors", type: "COLLECTION", icon: "⌁", id: "study · monitors",
    groups: [["Frequency domain", [["S-parameters", "2.20–2.70 GHz"], ["Input impedance", "Enabled"], ["Far field", "2.45 GHz"]]], ["Termination", [["Energy threshold", "−50 dB", "input"], ["Maximum steps", "12,000", "input"]]]]
  },
  "em.solver": {
    name: "Reference FDTD", type: "SOLVER", icon: "▶", id: "solver · fp64-cpu",
    groups: [["Engine", [["Precision", "FP64"], ["Backend", "CPU · 1 thread"], ["Update", "Yee leapfrog"], ["Boundary support", "PEC / Mur"]]], ["Estimate", [["Cells", "193,536"], ["Field memory", "37.2 MB"], ["Checkpoint", "5.4 MB"], ["Runtime", "00:18"]]]]
  }
};

const state = { mode: "cad", selection: { cad: "cad.fillet", em: "em.port" }, running: false, toastTimer: null };
const body = document.body;
const inspectorTitle = document.querySelector("#inspector-title");
const inspectorType = document.querySelector("#inspector-type");
const selectionName = document.querySelector("#selection-name");
const selectionId = document.querySelector("#selection-id");
const selectionIcon = document.querySelector("#selection-icon");
const inspectorContent = document.querySelector("#inspector-content");
const statusMessage = document.querySelector("#status-message");
const palette = document.querySelector("#command-palette");
const commandSearch = document.querySelector("#command-search");

function escapeHtml(value) {
  return String(value).replace(/[&<>"]/g, character => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", "\"": "&quot;" })[character]);
}

function propertyControl(label, value, kind) {
  if (kind === "input") {
    const match = String(value).match(/^(.+?)\s([^\s]+)$/);
    const number = match ? match[1] : value;
    const unit = match ? match[2] : "";
    return `<div class="field-control"><input aria-label="${escapeHtml(label)}" value="${escapeHtml(number)}"><span>${escapeHtml(unit)}</span></div>`;
  }
  if (kind === "select") {
    return `<select class="property-select" aria-label="${escapeHtml(label)}"><option>${escapeHtml(value)}</option></select>`;
  }
  return `<span class="property-value ${kind || ""}">${escapeHtml(value)}</span>`;
}

function renderInspector(key) {
  const data = selectionData[key];
  if (!data) return;
  inspectorTitle.textContent = data.name;
  inspectorType.textContent = data.type;
  selectionName.textContent = data.name;
  selectionId.textContent = data.id;
  selectionIcon.textContent = data.icon;
  inspectorContent.innerHTML = data.groups.map(([name, rows], groupIndex) => `
    <details class="property-section" open>
      <summary>${escapeHtml(name)}</summary>
      <div class="property-list">
        ${rows.map(([label, value, kind]) => `<div class="property-row"><span>${escapeHtml(label)}</span>${propertyControl(label, value, kind)}</div>`).join("")}
      </div>
      ${groupIndex === data.groups.length - 1 && (key === "cad.fillet" || key === "em.port") ? `<div class="identity-block">persistent-id: ${key === "cad.fillet" ? "feature/fillet-01/result-face-04" : "role/port/coax-feed-01"}<br>authority: evaluated-revision/18</div>` : ""}
    </details>`).join("");
  statusMessage.textContent = `${data.name} selected`;
}

function selectEntity(key) {
  const nextMode = key.split(".")[0];
  if (nextMode !== state.mode) setMode(nextMode);
  state.selection[state.mode] = key;
  document.querySelectorAll(".tree-row").forEach(row => {
    const selected = row.dataset.select === key;
    row.classList.toggle("selected", selected);
    if (selected) row.setAttribute("aria-selected", "true"); else row.removeAttribute("aria-selected");
  });
  renderInspector(key);
}

function updateModeCopy() {
  document.querySelectorAll(".mode-copy").forEach(node => {
    const copy = node.dataset[state.mode];
    if (copy) node.textContent = copy;
  });
}

function setMode(mode) {
  if (mode !== "cad" && mode !== "em") return;
  state.mode = mode;
  body.dataset.mode = mode;
  document.querySelectorAll("[data-mode-target]").forEach(tab => {
    const active = tab.dataset.modeTarget === mode;
    tab.classList.toggle("active", active);
    tab.setAttribute("aria-pressed", String(active));
  });
  document.querySelectorAll("[data-mode-panel]").forEach(panel => panel.classList.toggle("active", panel.dataset.modePanel === mode));
  document.querySelectorAll(".scene").forEach(scene => scene.classList.toggle("active", scene.classList.contains(`scene-${mode}`)));
  const browserFilter = document.querySelector("#browser-filter");
  browserFilter.value = "";
  browserFilter.placeholder = mode === "cad" ? "Filter model" : "Filter study";
  document.querySelectorAll(".tree-row.filtered-out").forEach(row => row.classList.remove("filtered-out"));
  updateModeCopy();
  selectEntity(state.selection[mode]);
}

function showToast(title, message) {
  window.clearTimeout(state.toastTimer);
  document.querySelector("#toast-title").textContent = title;
  document.querySelector("#toast-message").textContent = message;
  const toast = document.querySelector(".toast");
  toast.classList.add("visible");
  state.toastTimer = window.setTimeout(() => toast.classList.remove("visible"), 2800);
}

function toggleViewportClass(className, button) {
  const viewport = document.querySelector(".viewport-canvas");
  const active = viewport.classList.toggle(className);
  button.classList.toggle("active", active);
  button.setAttribute("aria-pressed", String(active));
}

function switchDock(name) {
  document.querySelectorAll(".dock-tab").forEach(tab => {
    const active = tab.dataset.dock === name;
    tab.classList.toggle("active", active);
    tab.setAttribute("aria-selected", String(active));
  });
  document.querySelectorAll("[data-dock-view]").forEach(view => view.classList.toggle("active", view.dataset.dockView === name));
}

function runSimulation() {
  if (state.running) return;
  if (state.mode !== "em") setMode("em");
  state.running = true;
  const button = document.querySelector("#run-button");
  const label = button.querySelector("span");
  button.classList.add("running");
  button.style.setProperty("--progress", "0%");
  let progress = 0;
  label.textContent = "Preparing · 0%";
  statusMessage.textContent = "Reference solve preparing";
  const interval = window.setInterval(() => {
    progress += 4;
    button.style.setProperty("--progress", `${progress}%`);
    label.textContent = progress < 28 ? `Preparing · ${progress}%` : `Solving · ${progress}%`;
    statusMessage.textContent = `Reference FDTD preview · ${progress}%`;
    if (progress >= 100) {
      window.clearInterval(interval);
      state.running = false;
      button.classList.remove("running");
      label.textContent = "Run reference solve";
      statusMessage.textContent = "Preview solve complete · 12,000 steps";
      showToast("Preview solve complete", "Synthetic wireframe result stored for comparison");
    }
  }, 80);
}

document.querySelectorAll("[data-mode-target]").forEach(button => button.addEventListener("click", () => setMode(button.dataset.modeTarget)));
document.querySelectorAll("[data-select]").forEach(button => button.addEventListener("click", () => selectEntity(button.dataset.select)));
document.querySelector("[data-toggle-grid]").addEventListener("click", event => toggleViewportClass("grid-visible", event.currentTarget));
document.querySelector("[data-toggle-overlay]").addEventListener("click", event => toggleViewportClass("overlay-visible", event.currentTarget));
document.querySelector("[data-fit]").addEventListener("click", () => {
  document.querySelector(".viewport-canvas").classList.remove("view-top");
  showToast("View fitted", "Active geometry centered in the viewport");
});
document.querySelector("[data-view]").addEventListener("click", () => {
  document.querySelector(".viewport-canvas").classList.toggle("view-top");
  statusMessage.textContent = "Viewport orientation changed";
});
document.querySelectorAll(".dock-tab").forEach(tab => tab.addEventListener("click", () => switchDock(tab.dataset.dock)));
document.querySelector(".dock-collapse").addEventListener("click", () => body.classList.toggle("compact-dock"));
document.querySelector("#run-button").addEventListener("click", runSimulation);
document.querySelector("#apply-button").addEventListener("click", () => showToast("Wireframe change staged", "Engine transactions will own this action in the Qt shell"));

document.querySelector("#browser-filter").addEventListener("input", event => {
  const query = event.target.value.trim().toLowerCase();
  document.querySelectorAll(`.tree-view[data-mode-panel="${state.mode}"] .tree-row`).forEach(row => row.classList.toggle("filtered-out", query && !row.textContent.toLowerCase().includes(query)));
});

function openPalette() {
  if (!palette.open) palette.showModal();
  commandSearch.value = "";
  filterCommands("");
  window.setTimeout(() => commandSearch.focus(), 0);
}

function filterCommands(query) {
  let visible = 0;
  palette.querySelectorAll("[data-command]").forEach(button => {
    const matches = button.textContent.toLowerCase().includes(query.trim().toLowerCase());
    button.hidden = !matches;
    if (matches) visible += 1;
  });
  palette.querySelector(".command-empty").hidden = visible !== 0;
}

document.querySelector("[data-command-open]").addEventListener("click", openPalette);
commandSearch.addEventListener("input", event => filterCommands(event.target.value));
palette.querySelectorAll("[data-command]").forEach(button => button.addEventListener("click", () => {
  const command = button.dataset.command;
  palette.close();
  if (command === "mode-em") setMode("em");
  if (command === "mode-cad") setMode("cad");
  if (command === "toggle-grid") document.querySelector("[data-toggle-grid]").click();
  if (command === "preflight") {
    setMode("em");
    switchDock("primary");
    showToast("Preflight complete", "3 checks passed · 1 grid warning needs review");
  }
}));

document.addEventListener("keydown", event => {
  if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "k") {
    event.preventDefault();
    openPalette();
  }
  if (!palette.open && event.key.toLowerCase() === "g" && !/input|select|textarea/i.test(event.target.tagName)) document.querySelector("[data-toggle-grid]").click();
});

renderInspector(state.selection.cad);
