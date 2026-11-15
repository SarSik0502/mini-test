const list = document.querySelector("#questions");
const listMessage = document.querySelector("#list-message");
const template = document.querySelector("#question-template");
const reload = document.querySelector("#reload");
const form = document.querySelector("#question-form");
const fields = document.querySelector("#question-fields");
const formTitle = document.querySelector("#form-title");
const promptInput = document.querySelector("#prompt");
const optionInputs = Array.from(document.querySelectorAll(".question-option"));
const correctOption = document.querySelector("#correct-option");
const save = document.querySelector("#save");
const cancel = document.querySelector("#cancel");
const feedback = document.querySelector("#feedback");
const deleteDialog = document.querySelector("#delete-dialog");
const deletePrompt = document.querySelector("#delete-prompt");

const questionsUrl = "/api/questions";
let questions = [];
let editingId = null;
let questionToDelete = null;

async function request(url, options = {}) {
  const response = await fetch(url, {
    cache: "no-store",
    signal: AbortSignal.timeout(8000),
    ...options,
  });

  if (!response.ok) {
    const error = await response.json().catch(() => ({}));
    if (typeof error.detail === "string") {
      throw new Error(error.detail);
    }
    if (response.status === 422) {
      throw new Error("Проверьте текст вопроса и варианты ответа.");
    }
    throw new Error(`Ошибка сервера (${response.status})`);
  }

  return response.status === 204 ? undefined : response.json();
}

function showFeedback(message, isError = false) {
  feedback.textContent = message;
  feedback.dataset.error = String(isError);
}

function setBusy(busy) {
  fields.disabled = busy;
  reload.disabled = busy;
  for (const button of list.querySelectorAll("button")) {
    button.disabled = busy;
  }
}

function renderQuestions() {
  listMessage.hidden = questions.length > 0;
  listMessage.textContent = "Список пуст. Добавьте первый вопрос.";
  list.replaceChildren(...questions.map((question) => {
    const item = template.content.cloneNode(true);
    item.querySelector(".question-prompt").textContent = question.prompt;
    item.querySelector(".answer").textContent =
      `Правильный ответ: ${question.options[question.correct_option - 1]}`;
    item.querySelector(".edit").addEventListener("click", () => editQuestion(question));
    item.querySelector(".delete").addEventListener("click", () => {
      questionToDelete = question;
      deletePrompt.textContent = question.prompt;
      deleteDialog.returnValue = "";
      deleteDialog.showModal();
    });
    return item;
  }));
}

function editQuestion(question) {
  editingId = question.id;
  formTitle.textContent = "Изменить вопрос";
  promptInput.value = question.prompt;
  optionInputs.forEach((input, index) => {
    input.value = question.options[index];
  });
  correctOption.value = String(question.correct_option);
  cancel.hidden = false;
  save.textContent = "Сохранить изменения";
  showFeedback("");
  promptInput.focus();
}

function resetEditor() {
  editingId = null;
  form.reset();
  formTitle.textContent = "Новый вопрос";
  save.textContent = "Добавить вопрос";
  cancel.hidden = true;
}

async function loadQuestions() {
  setBusy(true);
  reload.hidden = true;
  listMessage.textContent = "Загружаем вопросы…";
  try {
    questions = await request(questionsUrl);
    renderQuestions();
  } catch (error) {
    listMessage.textContent = `Не удалось загрузить вопросы. ${error.message}`;
    reload.hidden = false;
  } finally {
    setBusy(false);
  }
}

form.addEventListener("submit", async (event) => {
  event.preventDefault();
  const id = editingId;
  const payload = {
    prompt: promptInput.value.trim(),
    options: optionInputs.map((input) => input.value.trim()),
    correct_option: Number(correctOption.value),
  };

  setBusy(true);
  showFeedback("");
  try {
    const question = await request(id === null ? questionsUrl : `${questionsUrl}/${id}`, {
      method: id === null ? "POST" : "PUT",
      headers: {"Content-Type": "application/json"},
      body: JSON.stringify(payload),
    });
    if (id === null) {
      questions.push(question);
    } else {
      questions = questions.map((item) => item.id === id ? question : item);
    }
    renderQuestions();
    resetEditor();
    showFeedback(id === null ? "Вопрос добавлен." : "Изменения сохранены.");
  } catch (error) {
    showFeedback(`Не удалось сохранить вопрос. ${error.message}`, true);
  } finally {
    setBusy(false);
  }
});

async function deleteQuestion(question) {
  setBusy(true);
  showFeedback("");
  try {
    await request(`${questionsUrl}/${question.id}`, {method: "DELETE"});
    questions = questions.filter((item) => item.id !== question.id);
    if (editingId === question.id) {
      resetEditor();
    }
    renderQuestions();
    showFeedback("Вопрос удалён.");
  } catch (error) {
    showFeedback(`Не удалось удалить вопрос. ${error.message}`, true);
  } finally {
    setBusy(false);
  }
}

deleteDialog.addEventListener("close", () => {
  if (deleteDialog.returnValue === "delete" && questionToDelete !== null) {
    deleteQuestion(questionToDelete);
  }
  questionToDelete = null;
});

reload.addEventListener("click", loadQuestions);
cancel.addEventListener("click", () => {
  resetEditor();
  promptInput.focus();
});
loadQuestions();
