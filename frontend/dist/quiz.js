const form = document.querySelector("#quiz-form");
const answerFields = document.querySelector("#answer-fields");
const questionList = document.querySelector("#quiz-questions");
const submit = document.querySelector("#submit-answers");
const reload = document.querySelector("#reload-quiz");
const message = document.querySelector("#quiz-message");
const resultPanel = document.querySelector("#result-panel");
const resultTitle = document.querySelector("#result-title");
const score = document.querySelector("#score");
const checkResult = document.querySelector("#check-result");

let version;
let submission;
let attemptId = new URLSearchParams(location.search).get("attempt");

function showMessage(text) {
  message.hidden = !text;
  message.textContent = text;
}

async function request(path, options) {
  const response = await fetch(path, {
    cache: "no-store",
    signal: AbortSignal.timeout(8000),
    ...options,
  });
  const data = await response.json().catch(() => null);
  if (!response.ok) {
    const error = new Error(data?.detail || `Ошибка сервера (${response.status})`);
    error.status = response.status;
    throw error;
  }
  return {data, headers: response.headers};
}

function renderQuestion(question, index) {
  const fieldset = document.createElement("fieldset");
  fieldset.className = "quiz-question";
  const legend = document.createElement("legend");
  legend.textContent = `${index + 1}. ${question.prompt}`;
  fieldset.append(legend);

  question.options.forEach((option, index) => {
    const label = document.createElement("label");
    label.className = "quiz-option";
    const input = document.createElement("input");
    input.type = "radio";
    input.name = String(question.id);
    input.value = String(index + 1);
    input.required = true;
    label.append(input, option);
    fieldset.append(label);
  });
  return fieldset;
}

async function loadQuiz() {
  showMessage("Загружаем вопросы…");
  reload.hidden = true;
  form.hidden = true;
  try {
    const {data, headers} = await request("/api/quiz");
    if (data.length === 0) {
      showMessage("В тесте пока нет вопросов.");
      return;
    }
    version = headers.get("X-Quiz-Version");
    questionList.replaceChildren(...data.map(renderQuestion));
    submission = undefined;
    answerFields.disabled = false;
    submit.disabled = false;
    submit.textContent = "Отправить ответы";
    form.hidden = false;
    showMessage("");
  } catch (error) {
    showMessage(`Не удалось загрузить тест. ${error.message}`);
    reload.hidden = false;
  }
}

async function waitForResult() {
  checkResult.hidden = true;
  showMessage("Проверяем ответы…");
  try {
    for (let count = 0; count < 40; count++) {
      const {data} = await request(`/api/attempts/${encodeURIComponent(attemptId)}`);
      if (data.status === "completed") {
        score.textContent = `${data.score} из ${data.total}`;
        score.hidden = false;
        showMessage("");
        return;
      }
      await new Promise((resolve) => setTimeout(resolve, 500));
    }
    showMessage("Ответы ещё проверяются.");
  } catch (error) {
    showMessage(`Не удалось получить результат. ${error.message}`);
  }
  checkResult.hidden = false;
}

function openResult() {
  form.hidden = true;
  resultPanel.hidden = false;
  resultTitle.focus();
  waitForResult();
}

form.addEventListener("submit", async (event) => {
  event.preventDefault();
  if (!submission) {
    const answers = Object.fromEntries(
      Array.from(new FormData(form), ([questionId, option]) => [questionId, Number(option)])
    );
    submission = {id: crypto.randomUUID(), version, answers};
  }

  answerFields.disabled = true;
  submit.disabled = true;
  showMessage("Отправляем ответы…");
  try {
    const {data} = await request("/api/attempts", {
      method: "POST",
      headers: {"Content-Type": "application/json"},
      body: JSON.stringify(submission),
    });
    attemptId = data.id;
    history.replaceState(null, "", `?attempt=${encodeURIComponent(attemptId)}`);
    openResult();
  } catch (error) {
    showMessage(error.message);
    if (error.status === 409 || error.status === 422) {
      form.hidden = true;
      reload.hidden = false;
    } else {
      submit.textContent = "Повторить отправку";
      submit.disabled = false;
    }
  }
});

reload.addEventListener("click", loadQuiz);
checkResult.addEventListener("click", waitForResult);
if (attemptId) {
  openResult();
} else {
  loadQuiz();
}
