const gradeBases = ["F", "E", "D", "C", "B", "A", "S", "SS", "SSS", "SSSS"];
const grades = gradeBases.flatMap((base) =>
  ["", "+", "++", "+++", "++++"].map((suffix) => `${base}${suffix}`),
);
grades[grades.length - 1] = "SXXXXX";

const upgrades = [
  {
    id: "pencil",
    name: "Caneta turbinada",
    description: "Mais pontos por exercício",
    icon: "✎",
    startingCost: 20,
    costGrowth: 1.5,
    type: "click",
    amount: 1,
  },
  {
    id: "lamp",
    name: "Luminária de estudos",
    description: "Pontos automáticos por segundo",
    icon: "☼",
    startingCost: 60,
    costGrowth: 1.55,
    type: "passive",
    amount: 1,
  },
  {
    id: "notebook",
    name: "Caderno caprichado",
    description: "+3 pontos por exercício",
    icon: "▤",
    startingCost: 150,
    costGrowth: 1.6,
    type: "click",
    amount: 3,
  },
  {
    id: "tutor",
    name: "Professor particular",
    description: "+5 pontos automáticos por segundo",
    icon: "♟",
    startingCost: 250,
    costGrowth: 1.65,
    type: "passive",
    amount: 5,
  },
  {
    id: "study-group",
    name: "Grupo de estudos",
    description: "+20 pontos automáticos por segundo",
    icon: "♧",
    startingCost: 900,
    costGrowth: 1.7,
    type: "passive",
    amount: 20,
  },
  {
    id: "calculator",
    name: "Calculadora científica",
    description: "+15 pontos por exercício",
    icon: "⌗",
    startingCost: 1500,
    costGrowth: 1.75,
    type: "click",
    amount: 15,
  },
];

const storageKey = "nota10-clicker-save";
const freshState = () => ({
  points: 0,
  upgradeLevels: Object.fromEntries(upgrades.map((upgrade) => [upgrade.id, 0])),
});
let state = loadState();

const elements = {
  points: document.querySelector("#points"),
  currentGrade: document.querySelector("#current-grade"),
  perSecond: document.querySelector("#per-second"),
  gradeDisplay: document.querySelector("#grade-display"),
  gradeCaption: document.querySelector("#grade-caption"),
  gradeNumber: document.querySelector("#grade-number"),
  levelPill: document.querySelector("#level-pill"),
  progressValue: document.querySelector("#progress-value"),
  progressFill: document.querySelector("#progress-fill"),
  progressTrack: document.querySelector(".progress-track"),
  clickValue: document.querySelector("#click-value"),
  upgradeCount: document.querySelector("#upgrade-count"),
  upgradeList: document.querySelector("#upgrade-list"),
  gradeTrack: document.querySelector("#grade-track"),
  milestoneSummary: document.querySelector("#milestone-summary"),
  saveStatus: document.querySelector("#save-status"),
};

function loadState() {
  try {
    const saved = JSON.parse(localStorage.getItem(storageKey));
    if (!saved || !Number.isFinite(saved.points) || saved.points < 0) return freshState();

    const upgradeLevels = Object.fromEntries(
      upgrades.map((upgrade) => {
        const legacyLevel = upgrade.id === "pencil"
          ? saved.clickLevel
          : upgrade.id === "lamp"
            ? saved.passiveLevel
            : 0;
        const level = saved.upgradeLevels?.[upgrade.id] ?? legacyLevel ?? 0;
        if (!Number.isInteger(level) || level < 0) {
          throw new TypeError(`Nível inválido para melhoria: ${upgrade.id}`);
        }
        return [upgrade.id, level];
      }),
    );
    return { points: saved.points, upgradeLevels };
  } catch (error) {
    console.warn("Não foi possível carregar o progresso salvo.", error);
  }
  return freshState();
}

function gradeThreshold(index) {
  if (index === 0) return 0;
  return Math.round(20 * 1.23 ** (index - 1));
}

function getGradeIndex() {
  let index = 0;
  for (let i = 1; i < grades.length; i += 1) {
    if (state.points < gradeThreshold(i)) break;
    index = i;
  }
  return index;
}

function getUpgradeCost(upgrade) {
  const level = state.upgradeLevels[upgrade.id];
  return Math.round(upgrade.startingCost * upgrade.costGrowth ** level);
}

function getClickPower() {
  return 1 + upgrades
    .filter((upgrade) => upgrade.type === "click")
    .reduce((total, upgrade) => total + state.upgradeLevels[upgrade.id] * upgrade.amount, 0);
}

function getPointsPerSecond() {
  return upgrades
    .filter((upgrade) => upgrade.type === "passive")
    .reduce((total, upgrade) => total + state.upgradeLevels[upgrade.id] * upgrade.amount, 0);
}

function formatNumber(number) {
  return new Intl.NumberFormat("pt-BR", {
    maximumFractionDigits: number < 100 ? 1 : 0,
  }).format(number);
}

function render() {
  const gradeIndex = getGradeIndex();
  const nextThreshold = gradeThreshold(Math.min(gradeIndex + 1, grades.length - 1));
  const currentThreshold = gradeThreshold(gradeIndex);
  const progress = gradeIndex === grades.length - 1
    ? 100
    : Math.min(100, ((state.points - currentThreshold) / (nextThreshold - currentThreshold)) * 100);
  const remaining = gradeIndex === grades.length - 1
    ? 0
    : Math.max(0, nextThreshold - state.points);
  const totalUpgrades = Object.values(state.upgradeLevels).reduce((total, level) => total + level, 0);

  elements.points.textContent = formatNumber(state.points);
  elements.currentGrade.textContent = grades[gradeIndex];
  elements.gradeDisplay.textContent = grades[gradeIndex];
  elements.gradeCaption.textContent = gradeIndex === grades.length - 1
    ? "A lenda da escola!"
    : gradeIndex >= 30
      ? "Você é brilhante!"
      : gradeIndex >= 15
        ? "Mandando muito bem!"
        : gradeIndex >= 5
          ? "Tá pegando o jeito!"
          : "Dá pra melhorar!";
  elements.gradeNumber.textContent = `${String(gradeIndex + 1).padStart(2, "0")} — 50`;
  elements.levelPill.textContent = `NÍVEL ${String(gradeIndex + 1).padStart(2, "0")} / 50`;
  elements.progressValue.textContent = gradeIndex === grades.length - 1
    ? "NOTA MÁXIMA!"
    : `${formatNumber(state.points - currentThreshold)} / ${formatNumber(nextThreshold - currentThreshold)} pts`;
  elements.progressFill.style.width = `${progress}%`;
  elements.progressTrack.setAttribute("aria-valuenow", String(Math.round(progress)));
  elements.clickValue.textContent = `+${formatNumber(getClickPower())}`;
  elements.perSecond.textContent = formatNumber(getPointsPerSecond());
  elements.upgradeCount.textContent = `${totalUpgrades} ${totalUpgrades === 1 ? "item" : "itens"}`;
  elements.milestoneSummary.textContent = `${gradeIndex + 1} de 50 conquistadas`;
  renderUpgrades();
  renderGradeTrack(gradeIndex);
}

function renderUpgrades() {
  elements.upgradeList.replaceChildren();
  for (const upgrade of upgrades) {
    const level = state.upgradeLevels[upgrade.id];
    const cost = getUpgradeCost(upgrade);
    const card = document.createElement("article");
    card.className = "upgrade-card";

    const icon = document.createElement("span");
    icon.className = `upgrade-icon${upgrade.type === "passive" ? " lamp" : ""}`;
    icon.setAttribute("aria-hidden", "true");
    icon.textContent = upgrade.icon;

    const info = document.createElement("div");
    info.className = "upgrade-info";
    const name = document.createElement("strong");
    name.textContent = `${upgrade.name} · Nv. ${level}`;
    const description = document.createElement("span");
    description.textContent = upgrade.description;
    info.append(name, description);

    const button = document.createElement("button");
    button.className = "upgrade-buy";
    button.type = "button";
    button.dataset.upgrade = upgrade.id;
    button.disabled = state.points < cost;
    button.textContent = `✦ ${formatNumber(cost)}`;
    button.setAttribute("aria-label", `Comprar ${upgrade.name} por ${formatNumber(cost)} pontos`);

    card.append(icon, info, button);
    elements.upgradeList.append(card);
  }
}

function renderGradeTrack(currentIndex) {
  elements.gradeTrack.replaceChildren();
  grades.forEach((grade, index) => {
    const chip = document.createElement("span");
    chip.className = "grade-chip";
    if (index < currentIndex) chip.classList.add("unlocked");
    if (index === currentIndex) {
      chip.classList.add("current");
      chip.setAttribute("aria-current", "step");
    }
    chip.textContent = grade;
    elements.gradeTrack.append(chip);
  });
}

function saveState() {
  try {
    localStorage.setItem(storageKey, JSON.stringify(state));
    elements.saveStatus.textContent = "progresso salvo";
  } catch (error) {
    elements.saveStatus.textContent = "não foi possível salvar";
    console.error("Não foi possível salvar o progresso.", error);
  }
}

document.querySelector("#study-button").addEventListener("click", () => {
  state.points += getClickPower();
  render();
  saveState();
});

elements.upgradeList.addEventListener("click", (event) => {
  const button = event.target.closest("[data-upgrade]");
  if (!button) return;
  const upgrade = upgrades.find((item) => item.id === button.dataset.upgrade);
  if (!upgrade) return;
  const cost = getUpgradeCost(upgrade);
  if (state.points < cost) return;

  state.points -= cost;
  state.upgradeLevels[upgrade.id] += 1;
  render();
  saveState();
});

document.querySelector("#reset-button").addEventListener("click", () => {
  if (!window.confirm("Quer mesmo apagar seu progresso e começar do F?")) return;
  state = freshState();
  render();
  saveState();
});

window.setInterval(() => {
  const income = getPointsPerSecond();
  if (income === 0) return;
  state.points += income;
  render();
  saveState();
}, 1000);

render();
