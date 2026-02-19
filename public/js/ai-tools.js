/**
 * AI Tools - Flashcards, Quiz, Key Terms, Paraphraser, Citation Generator
 * Screenshot Annotator (client-side canvas)
 */

// ══════════════════════════════════════════════════════════════════════════════
// Input Mode Toggles
// ══════════════════════════════════════════════════════════════════════════════

function toggleFlashcardsInput(mode) {
    const upload = document.getElementById('flashcards-upload-mode');
    const paste = document.getElementById('flashcards-paste-mode');
    if (mode === 'upload') {
        upload.classList.remove('hidden');
        paste.classList.add('hidden');
    } else {
        upload.classList.add('hidden');
        paste.classList.remove('hidden');
    }
}

function toggleQuizInput(mode) {
    const upload = document.getElementById('quiz-upload-mode');
    const paste = document.getElementById('quiz-paste-mode');
    if (mode === 'upload') {
        upload.classList.remove('hidden');
        paste.classList.add('hidden');
    } else {
        upload.classList.add('hidden');
        paste.classList.remove('hidden');
    }
}

function toggleKeyTermsInput(mode) {
    const upload = document.getElementById('keyterms-upload-mode');
    const paste = document.getElementById('keyterms-paste-mode');
    if (mode === 'upload') {
        upload.classList.remove('hidden');
        paste.classList.add('hidden');
    } else {
        upload.classList.add('hidden');
        paste.classList.remove('hidden');
    }
}

function toggleCitationInput(mode) {
    document.getElementById('citation-url-mode').classList.toggle('hidden', mode !== 'url');
    document.getElementById('citation-doi-mode').classList.toggle('hidden', mode !== 'doi');
    document.getElementById('citation-manual-mode').classList.toggle('hidden', mode !== 'manual');
}

// ══════════════════════════════════════════════════════════════════════════════
// Character Counters Initialization
// ══════════════════════════════════════════════════════════════════════════════

document.addEventListener('DOMContentLoaded', () => {
    const counters = [
        { input: 'flashcards-text-input', counter: 'flashcards-char-count' },
        { input: 'quiz-text-input', counter: 'quiz-char-count' },
        { input: 'keyterms-text-input', counter: 'keyterms-char-count' },
        { input: 'paraphrase-input', counter: 'paraphrase-char-count' }
    ];
    counters.forEach(({ input, counter }) => {
        const el = document.getElementById(input);
        const ct = document.getElementById(counter);
        if (el && ct) {
            el.addEventListener('input', () => {
                ct.textContent = el.value.length.toLocaleString();
            });
        }
    });
});

// ══════════════════════════════════════════════════════════════════════════════
// AI Flashcard Generator
// ══════════════════════════════════════════════════════════════════════════════

let flashcardsData = [];
let currentCardIndex = 0;
let cardFlipped = false;
let cardConfidence = {};   // realIndex -> 'known' | 'unsure' | 'unknown'
let activeTagFilter = null; // null = show all, string = filter to tag
let filteredIndices = [];  // indices into flashcardsData currently visible
let restudyMode = false;   // when true, skip 'known' cards

function processFlashcards() {
    const toolId = 'ai-flashcards';
    const inputMode = document.querySelector('.preset-grid[data-tool="flashcards-input-mode"] .preset-btn.active')?.dataset.val || 'upload';
    let count = document.querySelector('.preset-grid[data-tool="flashcards-count"] .preset-btn.active')?.dataset.val || '20';
    if (count === 'max') count = '0'; // backend interprets 0 as max-mode

    let text = '';
    if (inputMode === 'paste') {
        text = document.getElementById('flashcards-text-input')?.value?.trim() || '';
        if (text.length < 50) {
            showToast('Please paste at least 50 characters of content', 'error');
            return;
        }
    } else {
        const file = state.files[toolId];
        if (!file) {
            showToast('Please upload a file first', 'error');
            return;
        }
    }

    showProcessing(toolId, true);

    const formData = new FormData();
    if (inputMode === 'paste') {
        formData.append('text', text);
    } else {
        formData.append('file', state.files[toolId]);
    }
    formData.append('count', count);

    fetch('/api/tools/ai-flashcards', { method: 'POST', body: formData })
        .then(r => r.json())
        .then(data => {
            showProcessing(toolId, false);
            if (data.error) {
                showToast(data.error, 'error');
                showModelBadge(toolId, 'none');
                return;
            }
            if (data.flashcards && Array.isArray(data.flashcards)) {
                flashcardsData = data.flashcards;
                currentCardIndex = 0;
                cardFlipped = false;
                cardConfidence = {};
                activeTagFilter = null;
                filteredIndices = [];
                restudyMode = false;
                renderFlashcardsUI();
                if (data.model_used) showModelBadge(toolId, data.model_used);
            }
        })
        .catch(err => {
            showToast(err.message || 'Request failed', 'error');
            showProcessing(toolId, false);
        });
}

function buildFilteredIndices() {
    filteredIndices = flashcardsData
        .map((card, i) => {
            if (restudyMode && cardConfidence[i] === 'known') return null;
            if (activeTagFilter && card.tag !== activeTagFilter) return null;
            return i;
        })
        .filter(i => i !== null);
    // fallback: if filter yields nothing, show everything
    if (filteredIndices.length === 0) {
        filteredIndices = flashcardsData.map((_, i) => i);
    }
}

function renderFlashcardsUI() {
    buildFilteredIndices();
    const container = document.querySelector('.flashcards-result[data-tool="ai-flashcards"]');
    if (!container || !flashcardsData.length) return;
    if (currentCardIndex >= filteredIndices.length) currentCardIndex = 0;

    const realIdx = filteredIndices[currentCardIndex];
    const card = flashcardsData[realIdx];
    const conf = cardConfidence[realIdx];

    // Confidence summary stats
    const knownCount   = Object.values(cardConfidence).filter(v => v === 'known').length;
    const unsureCount  = Object.values(cardConfidence).filter(v => v === 'unsure').length;
    const unknownCount = Object.values(cardConfidence).filter(v => v === 'unknown').length;
    const ratedCount   = knownCount + unsureCount + unknownCount;
    const hasWeak      = unknownCount + unsureCount > 0;

    // Unique tags
    const allTags = [...new Set(flashcardsData.map(c => c.tag).filter(Boolean))];
    const showFilter = allTags.length > 1;

    const filterHtml = showFilter ? `
        <div class="fc-section">
            <div class="fc-section-label"><i class="fas fa-tag"></i> Filter by topic</div>
            <div class="flashcard-filter">
                <button class="filter-pill ${!activeTagFilter ? 'active' : ''}" onclick="setFlashcardTagFilter(null)">All <span class="pill-count">${flashcardsData.length}</span></button>
                ${allTags.map(tag => {
                    const cnt = flashcardsData.filter(c => c.tag === tag).length;
                    return `<button class="filter-pill ${activeTagFilter === tag ? 'active' : ''}" data-tag="${escapeHtml(tag)}" onclick="setFlashcardTagFilter(this.dataset.tag)">${escapeHtml(tag)} <span class="pill-count">${cnt}</span></button>`;
                }).join('')}
            </div>
        </div>` : '';

    const statsHtml = ratedCount > 0 ? `
        <div class="flashcard-stats">
            <span class="stat-known"><i class="fas fa-check-circle"></i> ${knownCount} Got it</span>
            <span class="stat-unsure"><i class="fas fa-minus-circle"></i> ${unsureCount} Almost</span>
            <span class="stat-unknown"><i class="fas fa-times-circle"></i> ${unknownCount} Missed</span>
            <span class="stat-total">${ratedCount} of ${flashcardsData.length} rated</span>
        </div>` : '';

    const confClass = conf === 'known' ? 'conf-known' : conf === 'unsure' ? 'conf-unsure' : conf === 'unknown' ? 'conf-unknown' : '';
    const restudyBtn = hasWeak && !restudyMode
        ? `<button class="btn-secondary" onclick="enterRestudyMode()"><i class="fas fa-redo"></i> Re-study weak (${unknownCount + unsureCount})</button>`
        : restudyMode
            ? `<button class="btn-secondary" onclick="exitRestudyMode()"><i class="fas fa-th"></i> All Cards</button>`
            : '';

    container.innerHTML = `
        <div class="flashcard-viewer">
            ${filterHtml}
            <div class="flashcard-progress">
                <span>Card ${currentCardIndex + 1} of ${filteredIndices.length}${restudyMode ? ' &mdash; <em>Re-study mode</em>' : ''}</span>
                <div class="flashcard-progress-bar">
                    <div class="flashcard-progress-fill" style="width:${((currentCardIndex + 1) / filteredIndices.length) * 100}%"></div>
                </div>
            </div>
            ${statsHtml}
            <div class="flashcard ${cardFlipped ? 'flipped' : ''} ${confClass}" onclick="flipCard()">
                <div class="flashcard-inner">
                    <div class="flashcard-front">
                        ${card.tag ? `<div class="flashcard-tag">${escapeHtml(card.tag)}</div>` : ''}
                        <div class="flashcard-label">Question</div>
                        <div class="flashcard-content">${escapeHtml(card.question)}</div>
                    </div>
                    <div class="flashcard-back">
                        ${card.tag ? `<div class="flashcard-tag">${escapeHtml(card.tag)}</div>` : ''}
                        <div class="flashcard-label">Answer</div>
                        <div class="flashcard-content">${escapeHtml(card.answer)}</div>
                    </div>
                </div>
            </div>
            ${cardFlipped ? '' : `<div class="flashcard-hint"><i class="fas fa-hand-pointer"></i> Click the card to reveal the answer — then rate yourself</div>`}
            <div class="flashcard-nav">
                <button class="btn-secondary" onclick="prevCard()" ${currentCardIndex === 0 ? 'disabled' : ''}><i class="fas fa-chevron-left"></i> Prev</button>
                <button class="btn-secondary" onclick="shuffleCards()"><i class="fas fa-random"></i> Shuffle</button>
                ${restudyBtn}
                <button class="btn-secondary" onclick="nextCard()" ${currentCardIndex === filteredIndices.length - 1 ? 'disabled' : ''}>Next <i class="fas fa-chevron-right"></i></button>
            </div>
            ${cardFlipped ? `
            <div class="fc-section fc-confidence-section">
                <div class="fc-section-label"><i class="fas fa-brain"></i> How well did you know this? <span class="fc-label-sub">— auto-advances to next card</span></div>
                <div class="flashcard-confidence">
                    <button class="conf-btn conf-known-btn ${conf === 'known' ? 'active' : ''}" onclick="rateCard('known')"><span class="conf-icon">✅</span><span class="conf-label">Got it</span></button>
                    <button class="conf-btn conf-unsure-btn ${conf === 'unsure' ? 'active' : ''}" onclick="rateCard('unsure')"><span class="conf-icon">😐</span><span class="conf-label">Almost</span></button>
                    <button class="conf-btn conf-unknown-btn ${conf === 'unknown' ? 'active' : ''}" onclick="rateCard('unknown')"><span class="conf-icon">❌</span><span class="conf-label">Missed it</span></button>
                </div>
            </div>` : ``}
            <div class="fc-section flashcard-export">
                <div class="fc-section-label"><i class="fas fa-file-export"></i> Export deck <span class="fc-label-sub">— import into your flashcard app</span></div>
                <div class="fc-export-row">
                    <button class="btn-secondary" onclick="exportFlashcardsAnki()" title="Export as Anki-compatible tab-separated .txt"><i class="fas fa-download"></i> Anki <span class="export-fmt">.txt</span></button>
                    <button class="btn-secondary" onclick="exportFlashcardsQuizlet()" title="Export as Quizlet-compatible CSV"><i class="fas fa-graduation-cap"></i> Quizlet <span class="export-fmt">.csv</span></button>
                    <button class="btn-secondary" onclick="exportFlashcardsJSON()" title="Export full deck as JSON"><i class="fas fa-code"></i> JSON <span class="export-fmt">.json</span></button>
                </div>
            </div>
        </div>
    `;
    container.classList.remove('hidden');
}

function rateCard(confidence) {
    const realIdx = filteredIndices[currentCardIndex];
    cardConfidence[realIdx] = confidence;
    // Auto-advance to next unrated or next card
    if (currentCardIndex < filteredIndices.length - 1) {
        currentCardIndex++;
        cardFlipped = false;
    }
    renderFlashcardsUI();
}

function setFlashcardTagFilter(tag) {
    activeTagFilter = tag;
    currentCardIndex = 0;
    cardFlipped = false;
    renderFlashcardsUI();
}

function enterRestudyMode() {
    restudyMode = true;
    currentCardIndex = 0;
    cardFlipped = false;
    const weakCount = Object.values(cardConfidence).filter(v => v !== 'known').length
        + (flashcardsData.length - Object.keys(cardConfidence).length);
    renderFlashcardsUI();
    showToast(`Re-studying ${filteredIndices.length} weak / unrated cards`, 'info');
}

function exitRestudyMode() {
    restudyMode = false;
    currentCardIndex = 0;
    cardFlipped = false;
    renderFlashcardsUI();
}

function flipCard() {
    cardFlipped = !cardFlipped;
    renderFlashcardsUI();
}

function nextCard() {
    if (currentCardIndex < filteredIndices.length - 1) {
        currentCardIndex++;
        cardFlipped = false;
        renderFlashcardsUI();
    }
}

function prevCard() {
    if (currentCardIndex > 0) {
        currentCardIndex--;
        cardFlipped = false;
        renderFlashcardsUI();
    }
}

function shuffleCards() {
    for (let i = flashcardsData.length - 1; i > 0; i--) {
        const j = Math.floor(Math.random() * (i + 1));
        [flashcardsData[i], flashcardsData[j]] = [flashcardsData[j], flashcardsData[i]];
    }
    currentCardIndex = 0;
    cardFlipped = false;
    renderFlashcardsUI();
    showToast('Cards shuffled!', 'success');
}

function exportFlashcardsAnki() {
    // Anki tab-separated format: front\tback
    let content = '';
    flashcardsData.forEach(card => {
        const front = card.question.replace(/\t/g, ' ').replace(/\n/g, '<br>');
        const back = card.answer.replace(/\t/g, ' ').replace(/\n/g, '<br>');
        content += `${front}\t${back}\n`;
    });
    downloadText(content, 'flashcards_anki.txt', 'text/plain');
    showToast('Anki deck exported', 'success');
}

function exportFlashcardsQuizlet() {
    // Quizlet CSV: Term,Definition (RFC 4180 quoting)
    const escape = s => '"' + s.replace(/"/g, '""').replace(/\n/g, ' ') + '"';
    let rows = ['Term,Definition'];
    flashcardsData.forEach(card => rows.push(`${escape(card.question)},${escape(card.answer)}`));
    downloadText(rows.join('\n'), 'flashcards_quizlet.csv', 'text/csv');
    showToast('Quizlet CSV exported — import via Quizlet → Create → Import', 'success');
}

function exportFlashcardsJSON() {
    const content = JSON.stringify(flashcardsData, null, 2);
    downloadText(content, 'flashcards.json', 'application/json');
    showToast('JSON exported', 'success');
}

// ══════════════════════════════════════════════════════════════════════════════
// AI Practice Quiz
// ══════════════════════════════════════════════════════════════════════════════

let quizData = [];
let quizAnswers = {};
let quizSubmitted = false;

function processQuiz() {
    const toolId = 'ai-quiz';
    const inputMode = document.querySelector('.preset-grid[data-tool="quiz-input-mode"] .preset-btn.active')?.dataset.val || 'upload';
    const count = document.querySelector('.preset-grid[data-tool="quiz-count"] .preset-btn.active')?.dataset.val || '10';
    const difficulty = document.querySelector('.preset-grid[data-tool="quiz-difficulty"] .preset-btn.active')?.dataset.val || 'medium';

    let text = '';
    if (inputMode === 'paste') {
        text = document.getElementById('quiz-text-input')?.value?.trim() || '';
        if (text.length < 50) {
            showToast('Please paste at least 50 characters of content', 'error');
            return;
        }
    } else {
        const file = state.files[toolId];
        if (!file) {
            showToast('Please upload a file first', 'error');
            return;
        }
    }

    showProcessing(toolId, true);

    const formData = new FormData();
    if (inputMode === 'paste') {
        formData.append('text', text);
    } else {
        formData.append('file', state.files[toolId]);
    }
    formData.append('count', count);
    formData.append('difficulty', difficulty);

    fetch('/api/tools/ai-quiz', { method: 'POST', body: formData })
        .then(r => r.json())
        .then(data => {
            showProcessing(toolId, false);
            if (data.error) {
                showToast(data.error, 'error');
                showModelBadge(toolId, 'none');
                return;
            }
            if (data.questions && Array.isArray(data.questions)) {
                quizData = data.questions;
                quizAnswers = {};
                quizSubmitted = false;
                renderQuizUI();
                if (data.model_used) showModelBadge(toolId, data.model_used);
            }
        })
        .catch(err => {
            showToast(err.message || 'Request failed', 'error');
            showProcessing(toolId, false);
        });
}

function renderQuizUI() {
    const container = document.querySelector('.quiz-result[data-tool="ai-quiz"]');
    if (!container || !quizData.length) return;

    let html = '<div class="quiz-container">';
    
    if (quizSubmitted) {
        const correct = quizData.filter((q, i) => quizAnswers[i] === q.correct).length;
        const percentage = Math.round((correct / quizData.length) * 100);
        const grade = percentage >= 90 ? 'A' : percentage >= 80 ? 'B' : percentage >= 70 ? 'C' : percentage >= 60 ? 'D' : 'F';
        html += `
            <div class="quiz-score">
                <div class="quiz-score-circle" style="--score:${percentage}">
                    <span class="quiz-score-value">${percentage}%</span>
                    <span class="quiz-score-grade">${grade}</span>
                </div>
                <div class="quiz-score-text">${correct} of ${quizData.length} correct</div>
            </div>
        `;
    }

    quizData.forEach((q, idx) => {
        const selected = quizAnswers[idx];
        const isCorrect = quizSubmitted && selected === q.correct;
        const showCorrect = quizSubmitted && selected !== q.correct;
        
        html += `<div class="quiz-question ${quizSubmitted ? (isCorrect ? 'correct' : 'incorrect') : ''}">
            <div class="quiz-question-header">
                <span class="quiz-question-num">Q${idx + 1}</span>
                <span class="quiz-question-text">${escapeHtml(q.question)}</span>
            </div>
            <div class="quiz-options">`;
        
        q.options.forEach((opt, optIdx) => {
            const letter = String.fromCharCode(65 + optIdx);
            const isSelected = selected === optIdx;
            const isAnswer = q.correct === optIdx;
            let optClass = 'quiz-option';
            if (quizSubmitted) {
                if (isAnswer) optClass += ' correct-answer';
                else if (isSelected) optClass += ' wrong-answer';
            } else if (isSelected) {
                optClass += ' selected';
            }
            
            html += `<button class="${optClass}" onclick="selectQuizAnswer(${idx}, ${optIdx})" ${quizSubmitted ? 'disabled' : ''}>
                <span class="quiz-option-letter">${letter}</span>
                <span class="quiz-option-text">${escapeHtml(opt)}</span>
                ${quizSubmitted && isAnswer ? '<i class="fas fa-check"></i>' : ''}
                ${quizSubmitted && isSelected && !isAnswer ? '<i class="fas fa-times"></i>' : ''}
            </button>`;
        });
        
        html += '</div>';
        
        if (quizSubmitted && q.explanation) {
            html += `<div class="quiz-explanation"><i class="fas fa-lightbulb"></i> ${escapeHtml(q.explanation)}</div>`;
        }
        
        html += '</div>';
    });

    if (!quizSubmitted) {
        html += `<button class="process-btn quiz-submit" onclick="submitQuiz()"><i class="fas fa-check-circle"></i> Submit Quiz</button>`;
    } else {
        html += `<button class="btn-secondary quiz-retry" onclick="retryQuiz()"><i class="fas fa-redo"></i> Try Again</button>`;
    }

    html += '</div>';
    container.innerHTML = html;
    container.classList.remove('hidden');
}

function selectQuizAnswer(questionIdx, optionIdx) {
    if (quizSubmitted) return;
    quizAnswers[questionIdx] = optionIdx;
    renderQuizUI();
}

function submitQuiz() {
    if (Object.keys(quizAnswers).length < quizData.length) {
        showToast('Please answer all questions before submitting', 'warning');
        return;
    }
    quizSubmitted = true;
    renderQuizUI();
    
    const correct = quizData.filter((q, i) => quizAnswers[i] === q.correct).length;
    const percentage = Math.round((correct / quizData.length) * 100);
    showToast(`Quiz complete! You scored ${percentage}%`, percentage >= 70 ? 'success' : 'warning');
}

function retryQuiz() {
    quizAnswers = {};
    quizSubmitted = false;
    renderQuizUI();
}

// ══════════════════════════════════════════════════════════════════════════════
// AI Key Terms Extractor
// ══════════════════════════════════════════════════════════════════════════════

let keyTermsData = [];

function processKeyTerms() {
    const toolId = 'ai-key-terms';
    const inputMode = document.querySelector('.preset-grid[data-tool="keyterms-input-mode"] .preset-btn.active')?.dataset.val || 'upload';

    let text = '';
    if (inputMode === 'paste') {
        text = document.getElementById('keyterms-text-input')?.value?.trim() || '';
        if (text.length < 50) {
            showToast('Please paste at least 50 characters of content', 'error');
            return;
        }
    } else {
        const file = state.files[toolId];
        if (!file) {
            showToast('Please upload a file first', 'error');
            return;
        }
    }

    showProcessing(toolId, true);

    const formData = new FormData();
    if (inputMode === 'paste') {
        formData.append('text', text);
    } else {
        formData.append('file', state.files[toolId]);
    }

    fetch('/api/tools/ai-key-terms', { method: 'POST', body: formData })
        .then(r => r.json())
        .then(data => {
            showProcessing(toolId, false);
            if (data.error) {
                showToast(data.error, 'error');
                return;
            }
            if (data.terms && Array.isArray(data.terms)) {
                keyTermsData = data.terms;
                renderKeyTermsUI();
            }
        })
        .catch(err => {
            showToast(err.message || 'Request failed', 'error');
            showProcessing(toolId, false);
        });
}

function renderKeyTermsUI() {
    const container = document.querySelector('.keyterms-result[data-tool="ai-key-terms"]');
    if (!container || !keyTermsData.length) return;

    let html = `
        <div class="keyterms-header">
            <span class="keyterms-count"><i class="fas fa-key"></i> ${keyTermsData.length} terms extracted</span>
            <button class="btn-secondary" onclick="exportKeyTerms()"><i class="fas fa-download"></i> Export</button>
        </div>
        <div class="keyterms-list">
    `;
    
    keyTermsData.forEach((term, idx) => {
        html += `
            <div class="keyterm-card">
                <div class="keyterm-term">${escapeHtml(term.term)}</div>
                <div class="keyterm-definition">${escapeHtml(term.definition)}</div>
                ${term.example ? `<div class="keyterm-example"><i class="fas fa-quote-left"></i> ${escapeHtml(term.example)}</div>` : ''}
            </div>
        `;
    });
    
    html += '</div>';
    container.innerHTML = html;
    container.classList.remove('hidden');
}

function exportKeyTerms() {
    let content = '# Key Terms\n\n';
    keyTermsData.forEach(term => {
        content += `## ${term.term}\n${term.definition}\n`;
        if (term.example) content += `> Example: ${term.example}\n`;
        content += '\n';
    });
    downloadText(content, 'key_terms.md', 'text/markdown');
    showToast('Key terms exported', 'success');
}

// ══════════════════════════════════════════════════════════════════════════════
// AI Paraphraser
// ══════════════════════════════════════════════════════════════════════════════

function processParaphrase() {
    const toolId = 'ai-paraphrase';
    const text = document.getElementById('paraphrase-input')?.value?.trim() || '';
    const tone = document.querySelector('.preset-grid[data-tool="paraphrase-tone"] .preset-btn.active')?.dataset.val || 'formal';

    if (text.length < 20) {
        showToast('Please enter at least 20 characters to paraphrase', 'error');
        return;
    }

    showProcessing(toolId, true);

    const formData = new FormData();
    formData.append('text', text);
    formData.append('tone', tone);

    fetch('/api/tools/ai-paraphrase', { method: 'POST', body: formData })
        .then(r => r.json())
        .then(data => {
            showProcessing(toolId, false);
            if (data.error) {
                showToast(data.error, 'error');
                showModelBadge(toolId, 'none');
                return;
            }
            if (data.result) {
                renderParaphraseUI(text, data.result, tone);
                if (data.model_used) showModelBadge(toolId, data.model_used);
            }
        })
        .catch(err => {
            showToast(err.message || 'Request failed', 'error');
            showProcessing(toolId, false);
        });
}

function renderParaphraseUI(original, result, tone) {
    const container = document.querySelector('.paraphrase-result[data-tool="ai-paraphrase"]');
    if (!container) return;

    const toneLabels = { formal: 'Formal', casual: 'Casual', simplified: 'Simplified', academic: 'Academic' };
    
    container.innerHTML = `
        <div class="paraphrase-output">
            <div class="paraphrase-header">
                <span><i class="fas fa-check-circle"></i> Paraphrased (${toneLabels[tone]})</span>
                <button class="btn-icon" onclick="copyParaphrase()" title="Copy"><i class="fas fa-copy"></i></button>
            </div>
            <div class="paraphrase-text" id="paraphrase-output-text">${escapeHtml(result)}</div>
            <div class="paraphrase-stats">
                <span><i class="fas fa-text-width"></i> Original: ${original.length} chars</span>
                <span><i class="fas fa-text-width"></i> Result: ${result.length} chars</span>
            </div>
        </div>
    `;
    container.classList.remove('hidden');
}

function copyParaphrase() {
    const text = document.getElementById('paraphrase-output-text')?.textContent || '';
    navigator.clipboard.writeText(text).then(() => {
        showToast('Copied to clipboard', 'success');
    });
}

// ══════════════════════════════════════════════════════════════════════════════
// Citation Generator
// ══════════════════════════════════════════════════════════════════════════════

function processCitation() {
    const toolId = 'citation-gen';
    const sourceType = document.querySelector('.preset-grid[data-tool="citation-source"] .preset-btn.active')?.dataset.val || 'url';
    const style = document.querySelector('.preset-grid[data-tool="citation-style"] .preset-btn.active')?.dataset.val || 'apa';

    const formData = new FormData();
    formData.append('source_type', sourceType);
    formData.append('style', style);

    if (sourceType === 'url') {
        const url = document.getElementById('citation-url-input')?.value?.trim() || '';
        if (!url) {
            showToast('Please enter a URL', 'error');
            return;
        }
        formData.append('url', url);
    } else if (sourceType === 'doi') {
        const doi = document.getElementById('citation-doi-input')?.value?.trim() || '';
        if (!doi) {
            showToast('Please enter a DOI', 'error');
            return;
        }
        formData.append('doi', doi);
    } else {
        const author = document.getElementById('cit-author')?.value?.trim() || '';
        const title = document.getElementById('cit-title')?.value?.trim() || '';
        const year = document.getElementById('cit-year')?.value?.trim() || '';
        const publisher = document.getElementById('cit-publisher')?.value?.trim() || '';
        const citUrl = document.getElementById('cit-url')?.value?.trim() || '';
        
        if (!title) {
            showToast('Please enter at least a title', 'error');
            return;
        }
        formData.append('author', author);
        formData.append('title', title);
        formData.append('year', year);
        formData.append('publisher', publisher);
        formData.append('url', citUrl);
    }

    showProcessing(toolId, true);

    fetch('/api/tools/citation-generate', { method: 'POST', body: formData })
        .then(r => r.json())
        .then(data => {
            showProcessing(toolId, false);
            if (data.error) {
                showToast(data.error, 'error');
                return;
            }
            if (data.citation) {
                renderCitationUI(data.citation, data.metadata || {}, style);
            }
        })
        .catch(err => {
            showToast(err.message || 'Request failed', 'error');
            showProcessing(toolId, false);
        });
}

function renderCitationUI(citation, metadata, style) {
    const container = document.querySelector('.citation-result[data-tool="citation-gen"]');
    if (!container) return;

    const styleLabels = { apa: 'APA', mla: 'MLA', chicago: 'Chicago', harvard: 'Harvard' };
    
    container.innerHTML = `
        <div class="citation-output">
            <div class="citation-header">
                <span><i class="fas fa-quote-left"></i> ${styleLabels[style]} Citation</span>
                <button class="btn-icon" onclick="copyCitation()" title="Copy"><i class="fas fa-copy"></i></button>
            </div>
            <div class="citation-text" id="citation-output-text">${escapeHtml(citation)}</div>
            ${metadata.title ? `
            <div class="citation-meta">
                <div class="citation-meta-item"><strong>Title:</strong> ${escapeHtml(metadata.title)}</div>
                ${metadata.author ? `<div class="citation-meta-item"><strong>Author:</strong> ${escapeHtml(metadata.author)}</div>` : ''}
                ${metadata.date ? `<div class="citation-meta-item"><strong>Date:</strong> ${escapeHtml(metadata.date)}</div>` : ''}
                ${metadata.site ? `<div class="citation-meta-item"><strong>Site:</strong> ${escapeHtml(metadata.site)}</div>` : ''}
            </div>
            ` : ''}
        </div>
    `;
    container.classList.remove('hidden');
}

function copyCitation() {
    const text = document.getElementById('citation-output-text')?.textContent || '';
    navigator.clipboard.writeText(text).then(() => {
        showToast('Citation copied to clipboard', 'success');
    });
}

// ══════════════════════════════════════════════════════════════════════════════
// Screenshot Annotator
// ══════════════════════════════════════════════════════════════════════════════

let annotatorState = {
    canvas: null,
    ctx: null,
    image: null,
    tool: 'select',
    color: '#ff3366',
    strokeWidth: 4,
    shapes: [],
    currentShape: null,
    isDragging: false,
    startX: 0,
    startY: 0,
    selectedShape: null,
    history: []
};

function initAnnotator(file) {
    const workspace = document.querySelector('.annotator-workspace[data-tool="screenshot-annotate"]');
    const canvas = document.getElementById('annotator-canvas');
    if (!workspace || !canvas) return;

    const ctx = canvas.getContext('2d');
    annotatorState.canvas = canvas;
    annotatorState.ctx = ctx;
    annotatorState.shapes = [];
    annotatorState.history = [];

    const img = new Image();
    img.onload = () => {
        annotatorState.image = img;
        
        // Set canvas size preserving aspect ratio
        const maxWidth = Math.min(800, window.innerWidth - 100);
        const maxHeight = 600;
        let width = img.width;
        let height = img.height;
        
        if (width > maxWidth) {
            height = (maxWidth / width) * height;
            width = maxWidth;
        }
        if (height > maxHeight) {
            width = (maxHeight / height) * width;
            height = maxHeight;
        }
        
        canvas.width = width;
        canvas.height = height;
        
        redrawCanvas();
        workspace.classList.remove('hidden');
        
        // Hide upload zone
        document.getElementById('uz-screenshot-annotate')?.classList.add('hidden');
    };
    img.src = URL.createObjectURL(file);

    // Setup event listeners
    canvas.onmousedown = handleCanvasMouseDown;
    canvas.onmousemove = handleCanvasMouseMove;
    canvas.onmouseup = handleCanvasMouseUp;
    canvas.onmouseleave = handleCanvasMouseUp;

    // Touch support
    canvas.ontouchstart = e => { e.preventDefault(); handleCanvasMouseDown(getTouchEvent(e)); };
    canvas.ontouchmove = e => { e.preventDefault(); handleCanvasMouseMove(getTouchEvent(e)); };
    canvas.ontouchend = e => { e.preventDefault(); handleCanvasMouseUp(getTouchEvent(e)); };

    // Tool buttons
    document.querySelectorAll('.ann-tool').forEach(btn => {
        btn.onclick = () => {
            document.querySelectorAll('.ann-tool').forEach(b => b.classList.remove('active'));
            btn.classList.add('active');
            annotatorState.tool = btn.dataset.tool;
            annotatorState.selectedShape = null;
            redrawCanvas();
        };
    });

    // Color & stroke
    document.getElementById('ann-color').onchange = e => annotatorState.color = e.target.value;
    document.getElementById('ann-stroke').onchange = e => annotatorState.strokeWidth = parseInt(e.target.value);

    // Actions
    document.querySelectorAll('.ann-action').forEach(btn => {
        btn.onclick = () => {
            if (btn.dataset.action === 'undo') undoAnnotation();
            else if (btn.dataset.action === 'clear') clearAnnotations();
        };
    });
}

function getTouchEvent(e) {
    const rect = annotatorState.canvas.getBoundingClientRect();
    const touch = e.touches[0] || e.changedTouches[0];
    return {
        offsetX: touch.clientX - rect.left,
        offsetY: touch.clientY - rect.top
    };
}

function handleCanvasMouseDown(e) {
    const { offsetX, offsetY } = e;
    annotatorState.isDragging = true;
    annotatorState.startX = offsetX;
    annotatorState.startY = offsetY;

    const tool = annotatorState.tool;
    
    if (tool === 'text') {
        const text = prompt('Enter text:');
        if (text) {
            annotatorState.shapes.push({
                type: 'text',
                x: offsetX,
                y: offsetY,
                text,
                color: annotatorState.color,
                fontSize: 16 + annotatorState.strokeWidth * 2
            });
            saveHistory();
            redrawCanvas();
        }
        annotatorState.isDragging = false;
        return;
    }

    if (tool !== 'select') {
        annotatorState.currentShape = {
            type: tool,
            x1: offsetX,
            y1: offsetY,
            x2: offsetX,
            y2: offsetY,
            color: annotatorState.color,
            strokeWidth: annotatorState.strokeWidth
        };
    }
}

function handleCanvasMouseMove(e) {
    if (!annotatorState.isDragging || !annotatorState.currentShape) return;
    
    const { offsetX, offsetY } = e;
    annotatorState.currentShape.x2 = offsetX;
    annotatorState.currentShape.y2 = offsetY;
    redrawCanvas();
}

function handleCanvasMouseUp(e) {
    if (annotatorState.currentShape) {
        annotatorState.shapes.push(annotatorState.currentShape);
        annotatorState.currentShape = null;
        saveHistory();
    }
    annotatorState.isDragging = false;
    redrawCanvas();
}

function redrawCanvas() {
    const { ctx, canvas, image, shapes, currentShape } = annotatorState;
    if (!ctx || !image) return;

    ctx.clearRect(0, 0, canvas.width, canvas.height);
    ctx.drawImage(image, 0, 0, canvas.width, canvas.height);

    [...shapes, currentShape].filter(Boolean).forEach(shape => drawShape(shape));
}

function drawShape(shape) {
    const ctx = annotatorState.ctx;
    ctx.strokeStyle = shape.color;
    ctx.fillStyle = shape.color;
    ctx.lineWidth = shape.strokeWidth || 4;
    ctx.lineCap = 'round';
    ctx.lineJoin = 'round';

    switch (shape.type) {
        case 'arrow':
            drawArrow(ctx, shape.x1, shape.y1, shape.x2, shape.y2);
            break;
        case 'rect':
            ctx.strokeRect(shape.x1, shape.y1, shape.x2 - shape.x1, shape.y2 - shape.y1);
            break;
        case 'circle':
            const rx = Math.abs(shape.x2 - shape.x1) / 2;
            const ry = Math.abs(shape.y2 - shape.y1) / 2;
            const cx = shape.x1 + (shape.x2 - shape.x1) / 2;
            const cy = shape.y1 + (shape.y2 - shape.y1) / 2;
            ctx.beginPath();
            ctx.ellipse(cx, cy, rx, ry, 0, 0, Math.PI * 2);
            ctx.stroke();
            break;
        case 'text':
            ctx.font = `bold ${shape.fontSize || 20}px Inter, sans-serif`;
            ctx.fillText(shape.text, shape.x, shape.y);
            break;
        case 'blur':
            const w = Math.abs(shape.x2 - shape.x1);
            const h = Math.abs(shape.y2 - shape.y1);
            const sx = Math.min(shape.x1, shape.x2);
            const sy = Math.min(shape.y1, shape.y2);
            if (w > 0 && h > 0) {
                ctx.filter = 'blur(10px)';
                ctx.drawImage(annotatorState.canvas, sx, sy, w, h, sx, sy, w, h);
                ctx.filter = 'none';
            }
            break;
        case 'highlight':
            ctx.fillStyle = shape.color + '40'; // 25% opacity
            ctx.fillRect(shape.x1, shape.y1, shape.x2 - shape.x1, shape.y2 - shape.y1);
            break;
    }
}

function drawArrow(ctx, x1, y1, x2, y2) {
    const headLength = 15;
    const angle = Math.atan2(y2 - y1, x2 - x1);

    ctx.beginPath();
    ctx.moveTo(x1, y1);
    ctx.lineTo(x2, y2);
    ctx.stroke();

    ctx.beginPath();
    ctx.moveTo(x2, y2);
    ctx.lineTo(x2 - headLength * Math.cos(angle - Math.PI / 6), y2 - headLength * Math.sin(angle - Math.PI / 6));
    ctx.lineTo(x2 - headLength * Math.cos(angle + Math.PI / 6), y2 - headLength * Math.sin(angle + Math.PI / 6));
    ctx.closePath();
    ctx.fill();
}

function saveHistory() {
    annotatorState.history.push(JSON.stringify(annotatorState.shapes));
    if (annotatorState.history.length > 50) annotatorState.history.shift();
}

function undoAnnotation() {
    if (annotatorState.shapes.length > 0) {
        annotatorState.shapes.pop();
        redrawCanvas();
    }
}

function clearAnnotations() {
    annotatorState.shapes = [];
    redrawCanvas();
}

function resetAnnotator() {
    const workspace = document.querySelector('.annotator-workspace[data-tool="screenshot-annotate"]');
    const uploadZone = document.getElementById('uz-screenshot-annotate');
    if (workspace) workspace.classList.add('hidden');
    if (uploadZone) uploadZone.classList.remove('hidden');
    annotatorState.shapes = [];
    annotatorState.image = null;
}

function downloadAnnotated() {
    const canvas = annotatorState.canvas;
    if (!canvas) return;
    
    canvas.toBlob(blob => {
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = 'annotated_screenshot.png';
        a.click();
        URL.revokeObjectURL(url);
        showToast('Image downloaded', 'success');
    }, 'image/png');
}

// ══════════════════════════════════════════════════════════════════════════════
// Utility Functions
// ══════════════════════════════════════════════════════════════════════════════

function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

function downloadText(content, filename, mimeType) {
    const blob = new Blob([content], { type: mimeType });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = filename;
    a.click();
    URL.revokeObjectURL(url);
}

// ══════════════════════════════════════════════════════════════════════════════
// Upload Zone Handler for Screenshot Annotator
// ══════════════════════════════════════════════════════════════════════════════

document.addEventListener('DOMContentLoaded', () => {
    const uploadZone = document.getElementById('uz-screenshot-annotate');
    if (!uploadZone) return;

    const input = uploadZone.querySelector('.upload-input');
    
    uploadZone.addEventListener('click', () => input.click());
    
    input.addEventListener('change', e => {
        if (e.target.files[0]) {
            initAnnotator(e.target.files[0]);
        }
    });

    uploadZone.addEventListener('dragover', e => {
        e.preventDefault();
        uploadZone.classList.add('dragover');
    });

    uploadZone.addEventListener('dragleave', () => {
        uploadZone.classList.remove('dragover');
    });

    uploadZone.addEventListener('drop', e => {
        e.preventDefault();
        uploadZone.classList.remove('dragover');
        if (e.dataTransfer.files[0]) {
            initAnnotator(e.dataTransfer.files[0]);
        }
    });
});
