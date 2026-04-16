/* ============================================================
   BondUp – Friendship Network  |  JavaScript (Full-Stack)
   File: app.js
   Backend: C++ / cpp-httplib → http://localhost:8080
   ============================================================ */
'use strict';

/* ════════════════════════════════════════════════════════════
   1. CONFIG & API HELPERS
════════════════════════════════════════════════════════════ */
const API = '';
let authToken    = '';
let serverOnline = false;

async function checkServerStatus() {
  const badge = document.getElementById('serverBadge');
  if (!badge) return;
  badge.textContent = '⏳ Connecting to server…';
  badge.className   = 'server-badge checking';
  try {
    const res = await fetch(API + '/api/health', { method: 'GET', signal: AbortSignal.timeout(4000) });
    if (res.ok) { serverOnline = true; badge.textContent = '🟢 Server online'; badge.className = 'server-badge online'; }
    else throw new Error('bad');
  } catch (e) {
    serverOnline = false;
    badge.textContent = '🔴 Server offline — Demo mode active';
    badge.className   = 'server-badge offline';
  }
}

async function apiFetch(path, options = {}) {
  const headers = {
    'Content-Type': 'application/json',
    ...(authToken ? { 'Authorization': `Bearer ${authToken}` } : {}),
    ...(options.headers || {})
  };
  let res;
  try {
    res = await fetch(API + path, { ...options, headers, signal: AbortSignal.timeout(8000) });
  } catch (netErr) {
    throw new Error(netErr.name === 'AbortError' ? 'Request timed out' : 'Cannot reach server.');
  }
  const data = await res.json().catch(() => ({}));
  if (!res.ok) throw new Error(data.error || `Server error (HTTP ${res.status})`);
  return data;
}

/* ════════════════════════════════════════════════════════════
   2. FLOATING DOTS
════════════════════════════════════════════════════════════ */
const dotsBg = document.getElementById('dotsBg');
['#f7376e','#ff8c42','#a78bfa','#4ade80','#38bdf8'].forEach(c => {
  for (let i = 0; i < 7; i++) {
    const d = document.createElement('div'); d.className = 'dot';
    const s = Math.random() * 10 + 4;
    d.style.cssText = `width:${s}px;height:${s}px;background:${c};left:${Math.random()*100}%;bottom:-${s}px;animation-duration:${7+Math.random()*10}s;animation-delay:${Math.random()*9}s;`;
    dotsBg.appendChild(d);
  }
});

/* ════════════════════════════════════════════════════════════
   3. CORE HELPERS
════════════════════════════════════════════════════════════ */
const save = (k, v) => { try { localStorage.setItem(k, JSON.stringify(v)); } catch(e) {} };
const load = (k, fb) => { try { const v = localStorage.getItem(k); return v ? JSON.parse(v) : fb; } catch(e) { return fb; } };

function showToast(msg, icon = '✅', dur = 2800) {
  const t = document.getElementById('toast');
  document.getElementById('toastMsg').textContent = msg;
  document.getElementById('toastIcon').textContent = icon;
  t.classList.add('show'); setTimeout(() => t.classList.remove('show'), dur);
}

function setError(id, msg) {
  const el = document.getElementById(id); if (!el) return;
  el.textContent = msg; el.classList.toggle('show', !!msg);
}

function goTo(id) {
  document.querySelectorAll('.page').forEach(p => p.classList.remove('active'));
  document.getElementById(id).classList.add('active'); window.scrollTo(0, 0);
  if (id === 'dashPage') { setTimeout(syncComposerAv, 50); loadFeed(); }
}

/* ════════════════════════════════════════════════════════════
   4. LOGIN & AUTHENTICATION
════════════════════════════════════════════════════════════ */
document.getElementById('togglePw').addEventListener('click', function() {
  const pw = document.getElementById('password'), hide = pw.type === 'password';
  pw.type = hide ? 'text' : 'password'; this.textContent = hide ? '🙈' : '👁️';
});

document.getElementById('loginBtn').addEventListener('click', function(e) {
  const r = document.createElement('span'); r.className = 'ripple';
  const rect = this.getBoundingClientRect(), sz = Math.max(rect.width, rect.height);
  r.style.cssText = `width:${sz}px;height:${sz}px;left:${e.clientX-rect.left-sz/2}px;top:${e.clientY-rect.top-sz/2}px`;
  this.appendChild(r); setTimeout(() => r.remove(), 600);
  handleLogin();
});
['username', 'password'].forEach(id => {
  document.getElementById(id).addEventListener('keydown', e => { if (e.key === 'Enter') handleLogin(); });
});

async function handleLogin() {
  const u = document.getElementById('username').value.trim();
  const p = document.getElementById('password').value;
  let ok = true; setError('usernameErr', ''); setError('passwordErr', '');
  if (!u) { setError('usernameErr', 'Username is required.'); ok = false; }
  else if (u.length < 3) { setError('usernameErr', 'At least 3 characters needed.'); ok = false; }
  if (!p) { setError('passwordErr', 'Password is required.'); ok = false; }
  else if (p.length < 6) { setError('passwordErr', 'Minimum 6 characters required.'); ok = false; }
  if (!ok) return;

  const btn = document.getElementById('loginBtn');
  document.getElementById('btnText').style.display = 'none';
  document.getElementById('btnSpinner').style.display = 'block';
  btn.disabled = true;

  if (!serverOnline) {
    await new Promise(r => setTimeout(r, 800));
    authToken = 'demo-token';
    loggedInUser = u.replace(/_/g, ' ').replace(/\b\w/g, c => c.toUpperCase());
    document.getElementById('dashUsername').textContent = loggedInUser;
    applyNavAvatar(); btn.disabled = false;
    document.getElementById('btnText').style.display = '';
    document.getElementById('btnSpinner').style.display = 'none';
    goTo('dashPage');
    showToast(`Welcome, ${loggedInUser}! (Demo mode 🔧)`, '🤝', 3500);
    loadDemoData();
    return;
  }

  try {
    const data = await apiFetch('/api/auth/login', { method: 'POST', body: JSON.stringify({ username: u, password: p }) });
    authToken    = data.token;
    loggedInUser = data.user.display_name || data.user.username;
    document.getElementById('dashUsername').textContent = loggedInUser;
    applyNavAvatar(); goTo('dashPage');
    showToast(`Welcome back, ${loggedInUser}! 🎉`, '🤝', 3000);
    loadFriendSuggestions(); loadMyFriends(); loadFriendRequests(); loadBirthdaysFromServer();
  } catch (err) {
    if (err.message.includes('Cannot reach') || err.message.includes('timed out'))
      setError('passwordErr', '⚠️ Server unreachable.');
    else setError('passwordErr', err.message);
  } finally {
    btn.disabled = false;
    document.getElementById('btnText').style.display = '';
    document.getElementById('btnSpinner').style.display = 'none';
  }
}

function applyNavAvatar() {
  const nav = document.getElementById('navAvatar');
  if (!nav) return;
  if (currentPhoto.type === 'emoji') { nav.innerHTML = currentPhoto.value; nav.style.fontSize = '1.1rem'; }
  else { nav.innerHTML = `<img src="${currentPhoto.value}" alt="profile"/>`; nav.style.fontSize = '0'; }
}

// Logout
document.getElementById('logoutBtn').addEventListener('click', async () => {
  try { await apiFetch('/api/auth/logout', { method: 'POST' }); } catch(_) {}
  authToken = ''; loggedInUser = 'You';
  document.getElementById('username').value = '';
  document.getElementById('password').value = '';
  hideAllPanels();
  goTo('loginPage');
  showToast('Logged out. See you soon! 👋', '👋');
});

/* ════════════════════════════════════════════════════════════
   5. SIGN-UP (real API)
════════════════════════════════════════════════════════════ */
const signupOverlay = document.getElementById('signupOverlay');
document.getElementById('signupLink').addEventListener('click', () => signupOverlay.classList.add('open'));
document.getElementById('signupCancel').addEventListener('click', closeSignup);
signupOverlay.addEventListener('click', e => { if (e.target === signupOverlay) closeSignup(); });

function closeSignup() {
  signupOverlay.classList.remove('open');
  ['regDisplayName','regEmail','regUsername','regPassword'].forEach(id => document.getElementById(id).value = '');
  ['regNameErr','regEmailErr','regUsernameErr','regPasswordErr'].forEach(id => setError(id, ''));
}

document.getElementById('signupSubmit').addEventListener('click', async () => {
  const name = document.getElementById('regDisplayName').value.trim();
  const email = document.getElementById('regEmail').value.trim();
  const uname = document.getElementById('regUsername').value.trim();
  const pw = document.getElementById('regPassword').value;
  let ok = true;
  ['regNameErr','regEmailErr','regUsernameErr','regPasswordErr'].forEach(id => setError(id, ''));

  if (!name) { setError('regNameErr', 'Name is required.'); ok = false; }
  if (!email || !/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email)) { setError('regEmailErr', 'Valid email required.'); ok = false; }
  if (!uname || uname.length < 3) { setError('regUsernameErr', 'Min 3 characters.'); ok = false; }
  if (!pw || pw.length < 6) { setError('regPasswordErr', 'Min 6 characters.'); ok = false; }
  if (!ok) return;

  if (!serverOnline) {
    closeSignup();
    document.getElementById('username').value = uname;
    showToast('Account created! Sign in to continue. (Demo)', '🎉');
    return;
  }

  try {
    const data = await apiFetch('/api/auth/register', {
      method: 'POST',
      body: JSON.stringify({ username: uname, password: pw, display_name: name, email })
    });
    authToken = data.token;
    loggedInUser = data.user.display_name || data.user.username;
    document.getElementById('dashUsername').textContent = loggedInUser;
    applyNavAvatar(); closeSignup(); goTo('dashPage');
    showToast(`Welcome to BondUp, ${loggedInUser}! 🎉`, '🤝', 3500);
    loadFriendSuggestions(); loadMyFriends();
  } catch (err) {
    setError('regUsernameErr', err.message);
  }
});

/* ════════════════════════════════════════════════════════════
   6. FORGOT PASSWORD MODAL (real API)
════════════════════════════════════════════════════════════ */
const fpOverlay = document.getElementById('modalOverlay');
document.getElementById('forgotLink').addEventListener('click', () => fpOverlay.classList.add('open'));
document.getElementById('forgotLink').addEventListener('keydown', e => { if (e.key === 'Enter' || e.key === ' ') fpOverlay.classList.add('open'); });
document.getElementById('modalCancel').addEventListener('click', closeFP);
fpOverlay.addEventListener('click', e => { if (e.target === fpOverlay) closeFP(); });

function closeFP() { fpOverlay.classList.remove('open'); document.getElementById('resetEmail').value = ''; setError('resetEmailErr', ''); }

document.getElementById('modalSend').addEventListener('click', async () => {
  const em = document.getElementById('resetEmail').value.trim();
  setError('resetEmailErr', '');
  if (!em) { setError('resetEmailErr', 'Please enter your email.'); return; }
  if (!/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(em)) { setError('resetEmailErr', 'Enter a valid email.'); return; }

  if (serverOnline) {
    try {
      const data = await apiFetch('/api/auth/forgot-password', { method: 'POST', body: JSON.stringify({ email: em }) });
      closeFP();
      showToast(data.message || 'Password reset! New password: reset123', '🔑', 5000);
    } catch (err) { setError('resetEmailErr', err.message); }
  } else {
    closeFP();
    showToast(`Reset link sent to ${em}`, '📬', 3200);
  }
});

/* ════════════════════════════════════════════════════════════
   7. PROFILE PHOTO PICKER (localStorage)
════════════════════════════════════════════════════════════ */
const PHOTO_PRESETS = ['😊','😎','🥳','🤩','😄','🧑‍💻','👩‍🎤','🧑‍🎓','🦸','🧙','🐱','🐶','🦊','🐼','🌸','⚡'];
let currentPhoto = load('bondup_photo', { type: 'emoji', value: '😊' });
let pendingPhoto  = null;

const presetGrid = document.getElementById('presetGrid');
PHOTO_PRESETS.forEach(em => {
  const item = document.createElement('div'); item.className = 'preset-item'; item.textContent = em;
  item.addEventListener('click', () => {
    document.querySelectorAll('.preset-item').forEach(x => x.classList.remove('selected'));
    item.classList.add('selected');
    pendingPhoto = { type: 'emoji', value: em }; refreshPhotoPreview(pendingPhoto);
  });
  presetGrid.appendChild(item);
});
applyPhotoEverywhere(currentPhoto);

function openPhotoModal() {
  pendingPhoto = { ...currentPhoto };
  document.querySelectorAll('.preset-item').forEach(x =>
    x.classList.toggle('selected', currentPhoto.type === 'emoji' && x.textContent === currentPhoto.value));
  refreshPhotoPreview(currentPhoto);
  document.getElementById('photoModalOverlay').classList.add('open');
}
function closePhotoModal() { document.getElementById('photoModalOverlay').classList.remove('open'); document.getElementById('photoInput').value = ''; pendingPhoto = null; }

document.getElementById('profileAvatarWrap').addEventListener('click', openPhotoModal);
document.getElementById('changePhotoBtn').addEventListener('click', openPhotoModal);
document.getElementById('photoCancel').addEventListener('click', closePhotoModal);
document.getElementById('photoModalOverlay').addEventListener('click', e => { if (e.target === document.getElementById('photoModalOverlay')) closePhotoModal(); });
document.getElementById('uploadZone').addEventListener('click', () => document.getElementById('photoInput').click());

const uz = document.getElementById('uploadZone');
uz.addEventListener('dragover', e => { e.preventDefault(); uz.style.borderColor = 'var(--accent3)'; uz.style.background = 'rgba(167,139,250,0.08)'; });
uz.addEventListener('dragleave', () => { uz.style.borderColor = ''; uz.style.background = ''; });
uz.addEventListener('drop', e => { e.preventDefault(); uz.style.borderColor=''; uz.style.background=''; if (e.dataTransfer.files[0]) handleImageFile(e.dataTransfer.files[0]); });
document.getElementById('photoInput').addEventListener('change', function() { if (this.files[0]) handleImageFile(this.files[0]); });

function handleImageFile(file) {
  const reader = new FileReader();
  reader.onload = ev => { pendingPhoto = { type: 'image', value: ev.target.result }; document.querySelectorAll('.preset-item').forEach(x => x.classList.remove('selected')); refreshPhotoPreview(pendingPhoto); };
  reader.readAsDataURL(file);
}

document.getElementById('photoApply').addEventListener('click', () => {
  if (!pendingPhoto) { closePhotoModal(); return; }
  currentPhoto = { ...pendingPhoto }; applyPhotoEverywhere(currentPhoto); save('bondup_photo', currentPhoto);
  closePhotoModal(); showToast('Profile photo updated! ✨', '📸');
});

function refreshPhotoPreview(photo) {
  const prev = document.getElementById('photoPreview');
  if (photo.type === 'image') prev.innerHTML = `<img src="${photo.value}" alt="preview"/>`;
  else { prev.innerHTML = photo.value; prev.style.background = 'linear-gradient(135deg,var(--accent2),var(--accent1))'; }
}

function applyPhotoEverywhere(photo) {
  const inner = document.getElementById('avatarInner');
  const nav   = document.getElementById('navAvatar');
  if (photo.type === 'image') {
    if (inner) inner.innerHTML = `<img src="${photo.value}" alt="profile"/>`;
    if (nav) { nav.innerHTML = `<img src="${photo.value}" alt="profile"/>`; nav.style.fontSize = '0'; }
  } else {
    if (inner) inner.innerHTML = photo.value;
    if (nav) { nav.innerHTML = photo.value; nav.style.fontSize = '1.1rem'; }
  }
}

/* ════════════════════════════════════════════════════════════
   8. DASHBOARD – PEOPLE / FRIENDS GRID
════════════════════════════════════════════════════════════ */
let loggedInUser = 'You';
const COVERS = [
  'linear-gradient(135deg,#34495e,#2c3e50)','linear-gradient(135deg,#f7376e,#ff8c42)',
  'linear-gradient(135deg,#e67e22,#f39c12)','linear-gradient(135deg,#8e44ad,#a78bfa)',
  'linear-gradient(135deg,#2980b9,#38bdf8)','linear-gradient(135deg,#e91e8c,#f7376e)',
  'linear-gradient(135deg,#1abc9c,#4ade80)','linear-gradient(135deg,#c0392b,#f7376e)',
];
function getCover(idx) { return COVERS[idx % COVERS.length]; }

let _cachedSuggestions = [];
let _cachedFriends     = [];

async function loadFriendSuggestions() {
  const grid = document.getElementById('peopleGrid');
  grid.innerHTML = '<p style="color:var(--muted);font-size:0.88rem;grid-column:1/-1;padding:12px 0;">Loading suggestions…</p>';
  try {
    const data = await apiFetch('/api/friends/suggestions');
    _cachedSuggestions = data.suggestions || [];
    renderPeopleGrid(_cachedSuggestions);
  } catch (err) { grid.innerHTML = `<p style="color:var(--error);font-size:0.88rem;grid-column:1/-1;">${err.message}</p>`; }
}

function renderPeopleGrid(list) {
  const grid = document.getElementById('peopleGrid'); grid.innerHTML = '';
  if (!list.length) { grid.innerHTML = '<p style="color:var(--muted);font-size:0.9rem;grid-column:1/-1;padding:20px 0;">No suggestions found 😊</p>'; return; }
  list.forEach((p, i) => {
    const card = document.createElement('div'); card.className = 'person-card'; card.id = `pc_${p.id}`;
    card.innerHTML = `
      <div class="person-cover" style="background:${getCover(i)};"></div>
      <div class="person-avatar" style="background:rgba(167,139,250,0.18);">${p.avatar_emoji || '😊'}</div>
      <div class="person-info">
        <h4>${p.display_name || p.username}</h4>
        <p>${p.mutual_friends ? p.mutual_friends + ' mutual friends' : '@' + p.username}</p>
        <button class="btn-add-friend" onclick="sendFriendRequest(${p.id}, this)">➕ Add Friend</button>
      </div>`;
    grid.appendChild(card);
  });
}

async function sendFriendRequest(userId, btn) {
  btn.textContent = '⏳ Sending…'; btn.disabled = true;
  try {
    await apiFetch(`/api/friends/request/${userId}`, { method: 'POST' });
    btn.textContent = '⏳ Request Sent'; btn.className = 'btn-add-friend sent';
    showToast('Friend request sent!', '✉️');
  } catch (err) { btn.textContent = '➕ Add Friend'; btn.disabled = false; showToast(err.message, '❌'); }
}

async function loadMyFriends() {
  const grid = document.getElementById('myFriendsGrid');
  grid.innerHTML = '<p style="color:var(--muted);font-size:0.88rem;grid-column:1/-1;padding:12px 0;">Loading…</p>';
  try {
    const data = await apiFetch('/api/friends');
    _cachedFriends = data.friends || [];
    renderMyFriends(_cachedFriends);
    document.getElementById('friendCount').textContent = _cachedFriends.length;
  } catch (err) { grid.innerHTML = `<p style="color:var(--error);font-size:0.88rem;grid-column:1/-1;">${err.message}</p>`; }
}

function renderMyFriends(list) {
  const grid = document.getElementById('myFriendsGrid'); grid.innerHTML = '';
  if (!list.length) { grid.innerHTML = '<p style="color:var(--muted);font-size:0.9rem;grid-column:1/-1;padding:20px 0;">No friends yet. Start connecting!</p>'; return; }
  list.forEach((f, i) => {
    const name = f.display_name || f.username || 'Friend';
    const emoji = f.avatar_emoji || '😊';
    const card = document.createElement('div'); card.className = 'person-card';
    card.innerHTML = `
      <div class="person-cover" style="background:${getCover(i)};"></div>
      <div class="person-avatar" style="background:rgba(74,222,128,0.18);">${emoji}</div>
      <div class="person-info">
        <h4>${name}</h4>
        <p>${f.mutual_friends ? f.mutual_friends + ' mutual' : 'Your friend'}</p>
        <div style="display:flex;gap:6px;margin-top:2px;">
          <button class="btn-add-friend friends" style="flex:1;" disabled>✅ Friends</button>
          <button class="btn-msg-friend" onclick="openSendMsg('${name}','${emoji}')" title="Send message">💬</button>
        </div>
      </div>`;
    grid.appendChild(card);
  });
}

document.getElementById('searchInput').addEventListener('keydown', e => { if (e.key === 'Enter') filterPeople(); });
function filterPeople() {
  const q = document.getElementById('searchInput').value.trim().toLowerCase();
  renderPeopleGrid(q ? _cachedSuggestions.filter(p => (p.display_name||p.username).toLowerCase().includes(q)) : _cachedSuggestions);
}

/* ════════════════════════════════════════════════════════════
   9. FRIEND REQUESTS
════════════════════════════════════════════════════════════ */
async function loadFriendRequests() {
  try {
    const data = await apiFetch('/api/friends/requests');
    const requests = data.requests || [];
    const list = document.getElementById('requestsList');
    const noReq = document.getElementById('noRequests');
    list.innerHTML = '';
    if (!requests.length) { noReq.style.display = 'block'; updateReqBadge(0); return; }
    noReq.style.display = 'none';
    requests.forEach(r => {
      const item = document.createElement('div'); item.className = 'req-item'; item.id = `req_${r.request_id}`;
      item.innerHTML = `
        <div class="req-avatar" style="background:rgba(167,139,250,0.15);">${r.avatar_emoji || '😊'}</div>
        <div class="req-info">
          <strong>${r.display_name || r.username}</strong>
          <span>${r.friend_count || 0} friends</span>
          <div class="req-btns">
            <button class="btn-confirm" onclick="acceptRequest(${r.request_id}, '${(r.display_name||r.username).replace(/'/g,"\\'")}')">Confirm</button>
            <button class="btn-decline" onclick="declineRequest(${r.request_id})">Delete</button>
          </div>
        </div>`;
      list.appendChild(item);
    });
    updateReqBadge(requests.length);
  } catch (err) { console.error('Failed to load requests:', err.message); }
}

function updateReqBadge(count) {
  const b = document.getElementById('reqBadge');
  b.textContent = count; b.style.display = count ? '' : 'none';
}

async function acceptRequest(requestId, name) {
  try {
    await apiFetch(`/api/friends/accept/${requestId}`, { method: 'POST' });
    document.getElementById(`req_${requestId}`)?.remove();
    showToast(`You and ${name} are now friends! 🎉`, '🤝');
    loadMyFriends(); loadFriendRequests();
  } catch (err) { showToast(err.message, '❌'); }
}

async function declineRequest(requestId) {
  try {
    await apiFetch(`/api/friends/decline/${requestId}`, { method: 'POST' });
    document.getElementById(`req_${requestId}`)?.remove();
    showToast('Request removed.', '🗑️');
    loadFriendRequests();
  } catch (err) { showToast(err.message, '❌'); }
}

/* ════════════════════════════════════════════════════════════
   10. PANEL MANAGEMENT & QUICK LINKS
════════════════════════════════════════════════════════════ */
function hideAllPanels() {
  document.getElementById('homeView').style.display = 'none';
  document.getElementById('bdayPanel').classList.remove('visible');
  document.getElementById('msgPanel').classList.remove('visible');
  document.getElementById('friendsPanel').classList.remove('visible');
  document.getElementById('photosPanel').classList.remove('visible');
  document.getElementById('eventsPanel').classList.remove('visible');
  document.getElementById('savedPanel').classList.remove('visible');
}

function showHomeView() {
  hideAllPanels();
  document.getElementById('homeView').style.display = '';
  setActiveQL('ql-home');
}

function setActiveQL(id) {
  document.querySelectorAll('.quick-link').forEach(l => l.classList.remove('active-link'));
  document.getElementById(id)?.classList.add('active-link');
}

/* Nav tabs */
document.querySelectorAll('.nav-btn').forEach((btn, idx) => {
  btn.addEventListener('click', function() {
    document.querySelectorAll('.nav-btn').forEach(b => b.classList.remove('active'));
    this.classList.add('active');
    if (idx === 0) showHomeView();
    else if (idx === 1) { setActiveQL('ql-friends'); showFriendsPanel(); }
    else if (idx === 2) { setActiveQL('ql-messages'); showMessagesPanel(); }
  });
});

/* Quick links */
document.getElementById('ql-home').addEventListener('click', showHomeView);
document.getElementById('ql-friends').addEventListener('click', () => { setActiveQL('ql-friends'); showFriendsPanel(); });
document.getElementById('ql-bday').addEventListener('click', () => { setActiveQL('ql-bday'); showBdayPanel(); });
document.getElementById('ql-photos').addEventListener('click', () => { setActiveQL('ql-photos'); showPhotosPanel(); });
document.getElementById('ql-events').addEventListener('click', () => { setActiveQL('ql-events'); showEventsPanel(); });
document.getElementById('ql-messages').addEventListener('click', () => { setActiveQL('ql-messages'); showMessagesPanel(); });
document.getElementById('ql-saved').addEventListener('click', () => { setActiveQL('ql-saved'); showSavedPanel(); });
document.getElementById('manageFriendsLink')?.addEventListener('click', () => { setActiveQL('ql-friends'); showFriendsPanel(); });

/* Back buttons */
document.getElementById('bdayBackBtn').addEventListener('click', showHomeView);
document.getElementById('msgBackBtn').addEventListener('click', showHomeView);
document.getElementById('friendsPanelBack').addEventListener('click', showHomeView);
document.getElementById('photosPanelBack').addEventListener('click', showHomeView);
document.getElementById('eventsPanelBack').addEventListener('click', showHomeView);
document.getElementById('savedPanelBack').addEventListener('click', showHomeView);

/* ════════════════════════════════════════════════════════════
   11. BIRTHDAY MANAGER
════════════════════════════════════════════════════════════ */
const BDAY_EMOJIS = ['😊','😎','🥳','🤩','😄','🧑‍💻','👩','👨','🎂','🌸','⭐','🦊','🐱','🎵','🏆','💫'];
let bdayPickedEmoji = '😊', bdayFilter = 'all', _bdayCache = [];

const bdayEmojiRow = document.getElementById('bdayEmojiRow');
BDAY_EMOJIS.forEach(em => {
  const btn = document.createElement('div');
  btn.className = 'bday-emoji-opt' + (em === bdayPickedEmoji ? ' picked' : '');
  btn.textContent = em;
  btn.addEventListener('click', () => { bdayPickedEmoji = em; document.querySelectorAll('.bday-emoji-opt').forEach(x => x.classList.remove('picked')); btn.classList.add('picked'); });
  bdayEmojiRow.appendChild(btn);
});

function showBdayPanel() { hideAllPanels(); document.getElementById('bdayPanel').classList.add('visible'); loadBirthdaysFromServer(); }

document.querySelectorAll('.bday-tab').forEach(tab => {
  tab.addEventListener('click', function() { document.querySelectorAll('.bday-tab').forEach(t => t.classList.remove('active')); this.classList.add('active'); bdayFilter = this.dataset.filter; renderBdayList(_bdayCache); });
});

async function loadBirthdaysFromServer() {
  try { const data = await apiFetch('/api/birthdays'); _bdayCache = data.birthdays || []; renderBdayList(_bdayCache); renderBdayAlerts(_bdayCache); }
  catch (err) { console.error('Birthday load failed:', err.message); }
}

document.getElementById('addBdayBtn').addEventListener('click', addBirthday);
document.getElementById('bdayName').addEventListener('keydown', e => { if (e.key === 'Enter') document.getElementById('bdayDate').focus(); });

async function addBirthday() {
  const nameEl = document.getElementById('bdayName'), dateEl = document.getElementById('bdayDate'), errEl = document.getElementById('bdayFormErr');
  const name = nameEl.value.trim(), date = dateEl.value;
  errEl.classList.remove('show'); errEl.textContent = '';
  if (!name) { errEl.textContent = "Enter your friend's name."; errEl.classList.add('show'); nameEl.focus(); return; }
  if (!date) { errEl.textContent = 'Select a birthday date.'; errEl.classList.add('show'); dateEl.focus(); return; }
  if (_bdayCache.some(b => b.friend_name.toLowerCase() === name.toLowerCase())) { errEl.textContent = `${name} already in your list!`; errEl.classList.add('show'); return; }
  const btn = document.getElementById('addBdayBtn'); btn.textContent = '⏳ Saving…'; btn.disabled = true;
  try {
    await apiFetch('/api/birthdays', { method: 'POST', body: JSON.stringify({ friend_name: name, birthday: date, emoji: bdayPickedEmoji }) });
    nameEl.value = ''; dateEl.value = '';
    showToast(`🎂 ${name}'s birthday added!`, '🎉');
    await loadBirthdaysFromServer();
  } catch (err) { errEl.textContent = err.message; errEl.classList.add('show'); }
  finally { btn.textContent = '🎉 Add Birthday'; btn.disabled = false; }
}

async function deleteBday(id) {
  const entry = _bdayCache.find(b => b.id === id);
  try { await apiFetch(`/api/birthdays/${id}`, { method: 'DELETE' }); if (entry) showToast(`Removed ${entry.friend_name}'s birthday.`, '🗑️'); await loadBirthdaysFromServer(); }
  catch (err) { showToast(err.message, '❌'); }
}

function getDaysUntil(dateStr) {
  const today = new Date(); today.setHours(0,0,0,0);
  const bd = new Date(dateStr + 'T00:00:00');
  const next = new Date(today.getFullYear(), bd.getMonth(), bd.getDate());
  if (next < today) next.setFullYear(today.getFullYear() + 1);
  return Math.round((next - today) / 86400000);
}
function fmtDate(dateStr) { return new Date(dateStr + 'T00:00:00').toLocaleDateString('en-IN', { day: 'numeric', month: 'long', year: 'numeric' }); }
function getFilteredBdays(list) {
  const today = new Date(); today.setHours(0,0,0,0);
  return list.filter(b => { if (bdayFilter==='all') return true; const d=getDaysUntil(b.birthday); if (bdayFilter==='upcoming') return d<=30; if (bdayFilter==='month') return new Date(b.birthday+'T00:00:00').getMonth()===today.getMonth(); return true; })
    .sort((a,b) => getDaysUntil(a.birthday)-getDaysUntil(b.birthday));
}

function renderBdayList(list) {
  const container = document.getElementById('bdayList');
  document.getElementById('bdayCountPill').textContent = list.length;
  const filtered = getFilteredBdays(list);
  if (!filtered.length) { container.innerHTML = `<div class="bday-empty"><div class="empty-icon">🎂</div><p>${list.length===0?'No birthdays added yet.<br/>Add your first friend above!':'No birthdays match this filter.'}</p></div>`; return; }
  container.innerHTML = '';
  filtered.forEach(b => {
    const days = getDaysUntil(b.birthday), isToday = days===0, isSoon = days>0&&days<=7;
    const entry = document.createElement('div');
    entry.className = 'bday-entry' + (isToday?' today-bday':isSoon?' soon-bday':'');
    entry.innerHTML = `
      <div class="bday-av" style="background:${isToday?'rgba(247,55,110,0.18)':isSoon?'rgba(255,140,66,0.18)':'rgba(167,139,250,0.12)'};">${b.emoji}</div>
      <div class="bday-info"><div class="bday-name-text">${b.friend_name}</div><div class="bday-date-text">📅 ${fmtDate(b.birthday)}</div></div>
      <div class="bday-right">${isToday?'<span class="today-tag">🎉 Today!</span>':`<div class="days-num">${days}</div><span class="days-lbl">days left</span>`}</div>
      <button class="bday-del" onclick="deleteBday(${b.id})" title="Remove">✕</button>`;
    container.appendChild(entry);
  });
}

function renderBdayAlerts(list) {
  const area = document.getElementById('bdayAlertArea'); area.innerHTML = '';
  (list||[]).filter(b => getDaysUntil(b.birthday)<=7).sort((a,b)=>getDaysUntil(a.birthday)-getDaysUntil(b.birthday)).slice(0,3).forEach(b => {
    const d = getDaysUntil(b.birthday), div = document.createElement('div'); div.className = 'bday-alert';
    div.innerHTML = `🎂 <strong>${b.friend_name}'s</strong> birthday is ${d===0?'<strong>today! 🎉</strong>':`in <strong>${d} day${d===1?'':'s'}</strong>!`}`;
    area.appendChild(div);
  });
}

/* ════════════════════════════════════════════════════════════
   12. POST FEATURE
════════════════════════════════════════════════════════════ */
let postImages = [], chosenMood = null, currentFeedFilter = 'all';

function syncComposerAv() {
  const inner = document.getElementById('avatarInner'); if (!inner) return;
  const cav = document.getElementById('composerAv'); if (cav) cav.innerHTML = inner.innerHTML;
  document.querySelectorAll('.comment-av').forEach(a => a.innerHTML = inner.innerHTML);
}

document.getElementById('photoAttachBtn').addEventListener('click', () => document.getElementById('postImgInput').click());
document.getElementById('postImgInput').addEventListener('change', function() {
  [...this.files].forEach(file => { if (!file.type.startsWith('image/')) return; const reader = new FileReader(); reader.onload = ev => { postImages.push(ev.target.result); renderImgPreview(); }; reader.readAsDataURL(file); });
  this.value = '';
});

function renderImgPreview() {
  const strip = document.getElementById('imgPreviewStrip'); strip.innerHTML = '';
  postImages.forEach((src, i) => {
    const wrap = document.createElement('div'); wrap.className = 'img-thumb-wrap';
    const img = document.createElement('img'); img.src = src;
    const btn = document.createElement('button'); btn.className = 'img-thumb-remove'; btn.textContent = '✕';
    btn.onclick = () => { postImages.splice(i, 1); renderImgPreview(); };
    wrap.appendChild(img); wrap.appendChild(btn); strip.appendChild(wrap);
  });
}

document.getElementById('feelingBtn').addEventListener('click', () => { const row = document.getElementById('moodRow'); row.style.display = row.style.display === 'none' ? 'flex' : 'none'; });
document.querySelectorAll('.mood-chip').forEach(chip => {
  chip.addEventListener('click', function() {
    if (this.classList.contains('chosen')) { this.classList.remove('chosen'); chosenMood = null; }
    else { document.querySelectorAll('.mood-chip').forEach(c => c.classList.remove('chosen')); this.classList.add('chosen'); chosenMood = this.dataset.mood; }
  });
});

document.querySelectorAll('.feed-filter-btn').forEach(btn => {
  btn.addEventListener('click', function() { document.querySelectorAll('.feed-filter-btn').forEach(b => b.classList.remove('active')); this.classList.add('active'); currentFeedFilter = this.dataset.ff; loadFeed(); });
});

document.getElementById('submitPostBtn').addEventListener('click', submitPost);
document.getElementById('postTextInput').addEventListener('keydown', e => { if (e.key === 'Enter' && (e.ctrlKey || e.metaKey)) submitPost(); });

async function submitPost() {
  const text = document.getElementById('postTextInput').value.trim();
  if (!text && postImages.length === 0) { showToast('Write something or add a photo!', '✏️'); return; }
  const btn = document.getElementById('submitPostBtn'); btn.textContent = '⏳ Posting…'; btn.disabled = true;
  try {
    await apiFetch('/api/posts', { method: 'POST', body: JSON.stringify({ text, mood: chosenMood || '', images: [...postImages] }) });
    document.getElementById('postTextInput').value = '';
    postImages = []; renderImgPreview(); chosenMood = null;
    document.querySelectorAll('.mood-chip').forEach(c => c.classList.remove('chosen'));
    document.getElementById('moodRow').style.display = 'none';
    showToast('Post shared! 🎉', '✅'); await loadFeed();
  } catch (err) { showToast(err.message || 'Failed to post.', '❌'); }
  finally { btn.textContent = 'Post'; btn.disabled = false; }
}

async function loadFeed() {
  const feed = document.getElementById('postFeed');
  if (!authToken) { feed.innerHTML = '<div class="feed-empty"><div class="fe-icon">📭</div><p>Log in to see your feed.</p></div>'; return; }
  try {
    const data = await apiFetch('/api/posts');
    let posts = data.posts || [];
    if (currentFeedFilter === 'photos') posts = posts.filter(p => p.type === 'photo');
    if (currentFeedFilter === 'text') posts = posts.filter(p => p.type === 'text');
    if (!posts.length) { feed.innerHTML = '<div class="feed-empty"><div class="fe-icon">📭</div><p>' + (data.posts?.length===0?'No posts yet.<br/>Share something!':'No posts match this filter.') + '</p></div>'; return; }
    feed.innerHTML = ''; posts.forEach(p => feed.appendChild(buildPostCard(p)));
  } catch (err) { feed.innerHTML = `<div class="feed-empty"><div class="fe-icon">⚠️</div><p>${err.message}</p></div>`; }
}

function buildPostCard(p) {
  const inner = document.getElementById('avatarInner');
  const avHTML = inner ? inner.innerHTML : '😊';
  const bigText = !p.images?.length && (p.text||'').length < 80;

  let imagesHTML = '';
  const imgs = p.images || [];
  if (imgs.length === 1) imagesHTML = `<div class="post-images post-img-single"><img src="${imgs[0]}" alt="" onclick="openLightbox(this.src)"/></div>`;
  else if (imgs.length === 2) imagesHTML = `<div class="post-images post-img-grid cols-2"><img src="${imgs[0]}" alt="" onclick="openLightbox(this.src)"/><img src="${imgs[1]}" alt="" onclick="openLightbox(this.src)"/></div>`;
  else if (imgs.length >= 3) { const extras = imgs.length - 3; imagesHTML = `<div class="post-images post-img-grid cols-3"><img class="img-main" src="${imgs[0]}" alt="" onclick="openLightbox(this.src)"/><img src="${imgs[1]}" alt="" onclick="openLightbox(this.src)"/><div style="position:relative;overflow:hidden;height:180px;"><img src="${imgs[2]}" alt="" style="width:100%;height:100%;object-fit:cover;display:block;" onclick="openLightbox(this.src)"/>${extras>0?`<div style="position:absolute;inset:0;background:rgba(15,12,26,0.62);display:flex;align-items:center;justify-content:center;font-size:1.6rem;font-weight:800;color:white;cursor:pointer;" onclick="openLightbox('${imgs[2]}')">+${extras}</div>`:''}</div></div>`; }

  const card = document.createElement('div'); card.className = 'post-card'; card.id = 'post_' + p.id;
  card.innerHTML = `
    <div class="post-header">
      <div class="post-av">${avHTML}</div>
      <div class="post-meta">
        <div class="post-author">${escapeHTML(p.author_name||'User')}</div>
        <div class="post-time-mood">${formatPostTime(p.created_at)}${p.mood?`<span class="post-mood-tag">${escapeHTML(p.mood)}</span>`:''}</div>
      </div>
      <button class="post-options-btn" onclick="toggleDropdown(${p.id})">⋯</button>
      <div class="post-dropdown" id="drop_${p.id}">
        <div class="drop-item" onclick="copyPost(${p.id}, \`${escapeHTML(p.text||'')}\`)">📋 Copy Text</div>
        <div class="drop-item" onclick="toggleSavePost(${p.id})">🔖 ${p.saved_by_me?'Unsave':'Save'} Post</div>
        <div class="drop-item danger" onclick="deletePost(${p.id})">🗑️ Delete Post</div>
      </div>
    </div>
    ${p.text?`<div class="post-text${bigText?' big-text':''}">${escapeHTML(p.text)}</div>`:''}
    ${imagesHTML}
    <div class="post-likes-bar" id="likeBar_${p.id}" style="${(p.likes||0)===0?'display:none':''}"><strong>${p.likes||0}</strong> ${(p.likes||0)===1?'person':'people'} liked this</div>
    <div class="post-actions">
      <button class="post-action-btn${p.liked_by_me?' liked':''}" id="likeBtn_${p.id}" onclick="toggleLike(${p.id})"><span class="pa-icon">${p.liked_by_me?'❤️':'🤍'}</span> Like${(p.likes||0)>0?' · '+p.likes:''}</button>
      <button class="post-action-btn" onclick="toggleComments(${p.id})"><span class="pa-icon">💬</span> Comment${(p.comments||0)>0?' · '+p.comments:''}</button>
      <button class="post-action-btn" onclick="sharePost(${p.id})"><span class="pa-icon">↗️</span> Share</button>
      <button class="post-action-btn${p.saved_by_me?' saved':''}" id="saveBtn_${p.id}" onclick="toggleSavePost(${p.id})"><span class="pa-icon">${p.saved_by_me?'🔖':'🏷️'}</span></button>
    </div>
    <div class="post-comments" id="comments_${p.id}">
      <div class="comment-input-row"><div class="comment-av">${avHTML}</div><input class="comment-input" id="cInput_${p.id}" placeholder="Write a comment… (Enter to send)"/><button class="comment-send-btn" onclick="addComment(${p.id})">➤</button></div>
      <div id="commentList_${p.id}"></div>
    </div>`;
  setTimeout(() => { const ci = document.getElementById('cInput_'+p.id); if (ci) ci.addEventListener('keydown', e => { if (e.key==='Enter') addComment(p.id); }); }, 0);
  return card;
}

/* Post interactions */
async function toggleLike(postId) {
  try {
    const data = await apiFetch(`/api/posts/${postId}/like`, { method: 'POST' });
    const btn = document.getElementById('likeBtn_'+postId), bar = document.getElementById('likeBar_'+postId);
    if (btn) { btn.className = 'post-action-btn'+(data.liked?' liked':''); btn.innerHTML = `<span class="pa-icon">${data.liked?'❤️':'🤍'}</span> Like${data.count>0?' · '+data.count:''}`; }
    if (bar) { bar.style.display = data.count===0?'none':''; bar.innerHTML = `<strong>${data.count}</strong> ${data.count===1?'person':'people'} liked this`; }
  } catch (err) { showToast(err.message, '❌'); }
}

async function toggleSavePost(postId) {
  try {
    const data = await apiFetch(`/api/posts/${postId}/save`, { method: 'POST' });
    const btn = document.getElementById('saveBtn_'+postId);
    if (btn) { btn.className = 'post-action-btn'+(data.saved?' saved':''); btn.innerHTML = `<span class="pa-icon">${data.saved?'🔖':'🏷️'}</span>`; }
    showToast(data.saved ? 'Post saved! 🔖' : 'Post unsaved.', data.saved ? '🔖' : '🏷️');
    document.getElementById('drop_'+postId)?.classList.remove('open');
  } catch (err) { showToast(err.message, '❌'); }
}

function sharePost(postId) {
  const postEl = document.getElementById('post_'+postId);
  const text = postEl?.querySelector('.post-text')?.textContent || '';
  const url = window.location.href;
  const shareText = text ? `${text}\n\nShared from BondUp: ${url}` : `Check this out on BondUp: ${url}`;
  navigator.clipboard?.writeText(shareText).then(() => showToast('Post copied to clipboard! 🔗', '↗️')).catch(() => showToast('Share link copied! 🔗', '↗️'));
}

function toggleComments(id) {
  const sec = document.getElementById('comments_'+id); if (!sec) return;
  const opening = !sec.classList.contains('open');
  sec.classList.toggle('open');
  if (opening) { loadComments(id); document.getElementById('cInput_'+id)?.focus(); }
}

async function loadComments(postId) {
  try { const data = await apiFetch(`/api/posts/${postId}/comments`); renderCommentList(postId, data.comments||[]); }
  catch (err) { console.error('Comments load failed:', err.message); }
}

async function addComment(postId) {
  const inp = document.getElementById('cInput_'+postId); if (!inp) return;
  const text = inp.value.trim(); if (!text) return; inp.value = '';
  try {
    await apiFetch(`/api/posts/${postId}/comment`, { method: 'POST', body: JSON.stringify({ text }) });
    await loadComments(postId);
    const data = await apiFetch(`/api/posts/${postId}/comments`);
    const btn = document.querySelector(`#post_${postId} .post-action-btn:nth-child(2)`);
    if (btn) btn.innerHTML = `<span class="pa-icon">💬</span> Comment · ${(data.comments||[]).length}`;
  } catch (err) { showToast(err.message, '❌'); }
}

function renderCommentList(postId, comments) {
  const list = document.getElementById('commentList_'+postId); if (!list) return;
  const inner = document.getElementById('avatarInner');
  const avHTML = inner ? inner.innerHTML : '😊';
  list.innerHTML = '';
  comments.forEach(c => {
    const div = document.createElement('div'); div.className = 'comment-item';
    div.innerHTML = `<div class="comment-av">${avHTML}</div><div class="comment-body"><div class="comment-author">${escapeHTML(c.author_name||'User')}</div><div class="comment-text">${escapeHTML(c.text)}</div><div class="comment-ts">${formatPostTime(c.created_at)}</div></div>`;
    list.appendChild(div);
  });
}

function toggleDropdown(id) {
  const drop = document.getElementById('drop_'+id); if (!drop) return;
  document.querySelectorAll('.post-dropdown.open').forEach(d => { if (d.id !== 'drop_'+id) d.classList.remove('open'); });
  drop.classList.toggle('open');
}
document.addEventListener('click', e => { if (!e.target.closest('.post-options-btn') && !e.target.closest('.post-dropdown')) document.querySelectorAll('.post-dropdown.open').forEach(d => d.classList.remove('open')); });

async function deletePost(id) {
  try { await apiFetch(`/api/posts/${id}`, { method: 'DELETE' }); showToast('Post deleted.', '🗑️'); await loadFeed(); }
  catch (err) { showToast(err.message, '❌'); }
}

function copyPost(id, text) {
  if (text) { navigator.clipboard?.writeText(text).catch(() => {}); showToast('Text copied!', '📋'); }
  else showToast('Nothing to copy.', 'ℹ️');
  document.getElementById('drop_'+id)?.classList.remove('open');
}

/* Lightbox */
function openLightbox(src) { document.getElementById('lightboxImg').src = src; document.getElementById('lightbox').classList.add('open'); }
document.getElementById('lightboxClose')?.addEventListener('click', () => document.getElementById('lightbox').classList.remove('open'));
document.getElementById('lightbox')?.addEventListener('click', e => { if (e.target.id === 'lightbox') document.getElementById('lightbox').classList.remove('open'); });

/* ════════════════════════════════════════════════════════════
   13. FRIENDS PANEL
════════════════════════════════════════════════════════════ */
let _fpCache = [];

function showFriendsPanel() {
  hideAllPanels();
  document.getElementById('friendsPanel').classList.add('visible');
  loadFriendsPanelData();
}

async function loadFriendsPanelData() {
  const grid = document.getElementById('friendsPanelGrid');
  grid.innerHTML = '<p style="color:var(--muted);text-align:center;padding:30px;">Loading friends…</p>';
  try {
    const data = await apiFetch('/api/friends');
    _fpCache = data.friends || [];
    document.getElementById('fpTotalFriends').textContent = _fpCache.length;
    const totalMutual = _fpCache.reduce((s,f) => s + (f.mutual_friends||0), 0);
    document.getElementById('fpMutualAvg').textContent = _fpCache.length ? Math.round(totalMutual / _fpCache.length) : 0;
    renderFriendsPanel(_fpCache);
  } catch (err) { grid.innerHTML = `<p style="color:var(--error);text-align:center;padding:30px;">${err.message}</p>`; }
}

function renderFriendsPanel(list) {
  const grid = document.getElementById('friendsPanelGrid'); grid.innerHTML = '';
  if (!list.length) { grid.innerHTML = '<p style="color:var(--muted);text-align:center;padding:40px;grid-column:1/-1;">No friends yet. Start connecting!</p>'; return; }
  list.forEach(f => {
    const name = f.display_name || f.username || 'Friend';
    const emoji = f.avatar_emoji || '😊';
    const card = document.createElement('div'); card.className = 'fp-card'; card.id = `fp_${f.id}`;
    card.innerHTML = `
      <div class="fp-avatar">${emoji}</div>
      <div class="fp-info">
        <div class="fp-name">${name}</div>
        <div class="fp-sub">${f.mutual_friends ? f.mutual_friends + ' mutual friends' : '@' + (f.username||'')}</div>
      </div>
      <div class="fp-actions">
        <button class="fp-msg-btn" onclick="openSendMsg('${name.replace(/'/g,"\\'")}','${emoji}')">💬 Chat</button>
        <button class="fp-remove-btn" onclick="removeFriend(${f.id},'${name.replace(/'/g,"\\'")}')">✕</button>
      </div>`;
    grid.appendChild(card);
  });
}

function filterFriendsPanel() {
  const q = document.getElementById('friendsPanelSearch').value.trim().toLowerCase();
  renderFriendsPanel(q ? _fpCache.filter(f => (f.display_name||f.username||'').toLowerCase().includes(q)) : _fpCache);
}

async function removeFriend(friendId, name) {
  if (!confirm(`Remove ${name} from your friends?`)) return;
  try {
    await apiFetch(`/api/friends/remove/${friendId}`, { method: 'POST' });
    document.getElementById(`fp_${friendId}`)?.remove();
    showToast(`${name} removed from friends.`, '👋');
    loadMyFriends(); loadFriendsPanelData(); loadFriendSuggestions();
  } catch (err) { showToast(err.message, '❌'); }
}

/* ════════════════════════════════════════════════════════════
   14. PHOTOS PANEL
════════════════════════════════════════════════════════════ */
function showPhotosPanel() {
  hideAllPanels();
  document.getElementById('photosPanel').classList.add('visible');
  loadPhotosGallery();
}

async function loadPhotosGallery() {
  const gallery = document.getElementById('photosGallery');
  const empty = document.getElementById('photosEmpty');
  gallery.innerHTML = ''; empty.style.display = 'none';
  try {
    const data = await apiFetch('/api/photos');
    const photos = data.photos || [];
    if (!photos.length) { empty.style.display = 'block'; return; }
    photos.forEach(ph => {
      const card = document.createElement('div'); card.className = 'photo-card';
      card.innerHTML = `<img src="${ph.src}" alt="Photo" onclick="openLightbox(this.src)"/><div class="photo-card-info"><strong>${escapeHTML(ph.author_name)}</strong><br/>${formatPostTime(ph.created_at)}</div>`;
      gallery.appendChild(card);
    });
  } catch (err) { gallery.innerHTML = `<p style="color:var(--error);text-align:center;padding:30px;">${err.message}</p>`; }
}

/* ════════════════════════════════════════════════════════════
   15. EVENTS PANEL
════════════════════════════════════════════════════════════ */
const EVENT_EMOJIS = ['📚','📸','🎉','🏆','🎵','🎮','🏃','🍕','☕','🎨','💻','🧪','🌿','🎭','🏕️','✈️'];
let eventPickedEmoji = '📚';

const eventEmojiRow = document.getElementById('eventEmojiRow');
EVENT_EMOJIS.forEach(em => {
  const btn = document.createElement('div');
  btn.className = 'event-emoji-opt' + (em === eventPickedEmoji ? ' picked' : '');
  btn.textContent = em;
  btn.addEventListener('click', () => { eventPickedEmoji = em; document.querySelectorAll('.event-emoji-opt').forEach(x => x.classList.remove('picked')); btn.classList.add('picked'); });
  eventEmojiRow.appendChild(btn);
});

function showEventsPanel() {
  hideAllPanels();
  document.getElementById('eventsPanel').classList.add('visible');
  loadEvents();
}

async function loadEvents() {
  try {
    const data = await apiFetch('/api/events');
    const events = data.events || [];
    document.getElementById('eventCountPill').textContent = events.length;
    renderEventList(events);
  } catch (err) { document.getElementById('eventList').innerHTML = `<p style="color:var(--error);text-align:center;padding:20px;">${err.message}</p>`; }
}

function renderEventList(events) {
  const container = document.getElementById('eventList');
  if (!events.length) { container.innerHTML = '<div class="event-empty"><div class="empty-icon">🏷️</div><p>No events yet.<br/>Create one above!</p></div>'; return; }
  container.innerHTML = '';
  events.forEach(e => {
    const entry = document.createElement('div'); entry.className = 'event-entry'; entry.id = `ev_${e.id}`;
    entry.innerHTML = `
      <div class="event-av">${e.emoji||'🏷️'}</div>
      <div class="event-info">
        <div class="event-title">${escapeHTML(e.title)}</div>
        ${e.description ? `<div class="event-desc">${escapeHTML(e.description)}</div>` : ''}
        <div class="event-meta">
          <span>📅 ${fmtDate(e.date)}</span>
          ${e.time ? `<span>🕐 ${e.time}</span>` : ''}
          ${e.location ? `<span>📍 ${escapeHTML(e.location)}</span>` : ''}
          <span>👥 ${e.attendees} going</span>
        </div>
      </div>
      <div class="event-actions">
        <button class="btn-attend${e.attending?' attending':''}" id="attendBtn_${e.id}" onclick="toggleAttend(${e.id})">${e.attending?'✅ Going':'🙋 Attend'}</button>
        <button class="event-del" onclick="deleteEvent(${e.id})" title="Delete">✕</button>
      </div>`;
    container.appendChild(entry);
  });
}

document.getElementById('createEventBtn').addEventListener('click', createEvent);

async function createEvent() {
  const title = document.getElementById('eventTitle').value.trim();
  const desc = document.getElementById('eventDesc').value.trim();
  const date = document.getElementById('eventDate').value;
  const time = document.getElementById('eventTime').value;
  const loc = document.getElementById('eventLocation').value.trim();
  const errEl = document.getElementById('eventFormErr');
  errEl.textContent = ''; errEl.classList.remove('show');
  if (!title) { errEl.textContent = 'Event title is required.'; errEl.classList.add('show'); return; }
  if (!date) { errEl.textContent = 'Please select a date.'; errEl.classList.add('show'); return; }

  const btn = document.getElementById('createEventBtn'); btn.textContent = '⏳ Creating…'; btn.disabled = true;
  try {
    await apiFetch('/api/events', { method: 'POST', body: JSON.stringify({ title, description: desc, date, time, location: loc, emoji: eventPickedEmoji }) });
    ['eventTitle','eventDesc','eventDate','eventTime','eventLocation'].forEach(id => document.getElementById(id).value = '');
    showToast('Event created! 🎉', '📅');
    await loadEvents();
  } catch (err) { errEl.textContent = err.message; errEl.classList.add('show'); }
  finally { btn.textContent = '📅 Create Event'; btn.disabled = false; }
}

async function toggleAttend(eventId) {
  try {
    const data = await apiFetch(`/api/events/${eventId}/attend`, { method: 'POST' });
    const btn = document.getElementById('attendBtn_'+eventId);
    if (btn) { btn.className = 'btn-attend'+(data.attending?' attending':''); btn.textContent = data.attending?'✅ Going':'🙋 Attend'; }
    showToast(data.attending ? "You're going! 🎉" : 'RSVP removed.', data.attending?'✅':'👋');
    await loadEvents();
  } catch (err) { showToast(err.message, '❌'); }
}

async function deleteEvent(eventId) {
  try { await apiFetch(`/api/events/${eventId}`, { method: 'DELETE' }); showToast('Event deleted.', '🗑️'); await loadEvents(); }
  catch (err) { showToast(err.message, '❌'); }
}

/* ════════════════════════════════════════════════════════════
   16. SAVED POSTS PANEL
════════════════════════════════════════════════════════════ */
function showSavedPanel() {
  hideAllPanels();
  document.getElementById('savedPanel').classList.add('visible');
  loadSavedPosts();
}

async function loadSavedPosts() {
  const feed = document.getElementById('savedFeed');
  const empty = document.getElementById('savedEmpty');
  feed.innerHTML = ''; empty.style.display = 'none';
  try {
    const data = await apiFetch('/api/posts/saved');
    const posts = data.posts || [];
    if (!posts.length) { empty.style.display = 'block'; return; }
    posts.forEach(p => feed.appendChild(buildPostCard(p)));
  } catch (err) { feed.innerHTML = `<div class="feed-empty"><div class="fe-icon">⚠️</div><p>${err.message}</p></div>`; }
}

/* ════════════════════════════════════════════════════════════
   17. MESSAGING (in-memory)
════════════════════════════════════════════════════════════ */
let conversations = {}, activeChatId = null;
const MSG_EMOJIS = ['😂','❤️','👍','🎉','😎','🔥','💯','🙌','😊','✨','👏','🫂'];

function showMessagesPanel() { hideAllPanels(); document.getElementById('msgPanel').classList.add('visible'); renderConvList(); syncMsgAvatar(); }

function syncMsgAvatar() { const inner = document.getElementById('avatarInner'); const av = inner ? inner.innerHTML : '😊'; const inputAv = document.getElementById('msgInputAv'); if (inputAv) inputAv.innerHTML = av; }

function openSendMsg(name, emoji) {
  activeSendTarget = { name, emoji };
  document.getElementById('sendMsgAv').textContent = emoji;
  document.getElementById('sendMsgName').textContent = name;
  document.getElementById('sendMsgInput').value = '';
  const cid = getConvId(name), conv = conversations[cid];
  const preview = document.getElementById('sendMsgPreview'); preview.innerHTML = '';
  if (conv && conv.messages.length) { conv.messages.slice(-3).forEach(m => { const d = document.createElement('div'); d.className = 'send-msg-bubble '+(m.from==='me'?'from-me':'from-them'); d.textContent = m.text; preview.appendChild(d); }); }
  else preview.innerHTML = '<p style="color:var(--muted);font-size:0.82rem;text-align:center;">Start a conversation with '+name+'!</p>';
  const qr = document.getElementById('sendMsgQuickRow'); qr.innerHTML = '';
  ["Hey! 👋","How are you?","What's up? 😊","Let's catch up!","Miss you! 💙","Happy birthday! 🎂"].forEach(txt => { const btn = document.createElement('button'); btn.className='quick-reply-chip'; btn.textContent=txt; btn.onclick=()=>{ document.getElementById('sendMsgInput').value=txt; document.getElementById('sendMsgInput').focus(); }; qr.appendChild(btn); });
  document.getElementById('sendMsgOverlay').classList.add('open');
  setTimeout(() => document.getElementById('sendMsgInput').focus(), 100);
}

let activeSendTarget = null;
document.getElementById('sendMsgClose').addEventListener('click', closeSendMsg);
document.getElementById('sendMsgOverlay').addEventListener('click', e => { if (e.target === document.getElementById('sendMsgOverlay')) closeSendMsg(); });
function closeSendMsg() { document.getElementById('sendMsgOverlay').classList.remove('open'); activeSendTarget = null; }

document.getElementById('sendMsgBtn').addEventListener('click', () => {
  const txt = document.getElementById('sendMsgInput').value.trim();
  if (!txt || !activeSendTarget) return;
  sendMessageTo(activeSendTarget.name, activeSendTarget.emoji, txt);
  document.getElementById('sendMsgInput').value = '';
  closeSendMsg(); showToast(`Message sent to ${activeSendTarget.name}! 💬`, '✉️');
});
document.getElementById('sendMsgInput').addEventListener('keydown', e => { if (e.key === 'Enter') document.getElementById('sendMsgBtn').click(); });

function sendMessageTo(name, emoji, text) {
  const cid = getConvId(name);
  if (!conversations[cid]) conversations[cid] = { id: cid, name, emoji, messages: [], unread: 0 };
  const conv = conversations[cid];
  conv.messages.push({ from: 'me', text, time: new Date() });
  const replies = ["Hey! 😊","That's great!","Haha 😂","Sure, let's do it!","Miss you too! 💙","Sounds good 👍","I'll be there!","❤️","Thanks for reaching out!","Let's catch up soon!","lol 😂","OMG yes!!","Can't wait 🎉"];
  setTimeout(() => { conv.messages.push({ from: 'them', text: replies[Math.floor(Math.random()*replies.length)], time: new Date() }); if (activeChatId===cid) renderBubbles(cid); else conv.unread=(conv.unread||0)+1; renderConvList(); updateMsgBadge(); }, 1200+Math.random()*2000);
  renderConvList(); if (activeChatId===cid) renderBubbles(cid); updateMsgBadge();
}

function getConvId(name) { return name.toLowerCase().replace(/\s+/g, '_'); }
function filterConversations() { renderConvList(document.getElementById('msgSearch').value.trim().toLowerCase()); }

function renderConvList(filter = '') {
  const list = document.getElementById('convList'), empty = document.getElementById('convEmpty');
  list.innerHTML = '';
  const all = Object.values(conversations).filter(c => !filter || c.name.toLowerCase().includes(filter)).sort((a,b) => { const aL=a.messages[a.messages.length-1]?.time||0, bL=b.messages[b.messages.length-1]?.time||0; return bL-aL; });
  if (!all.length) { empty.style.display = 'flex'; return; }
  empty.style.display = 'none';
  all.forEach(conv => {
    const lastMsg = conv.messages[conv.messages.length-1];
    const preview = lastMsg ? (lastMsg.from==='me'?'You: ':'')+lastMsg.text : 'No messages yet';
    const item = document.createElement('div'); item.className = 'conv-item'+(conv.id===activeChatId?' active-conv':'');
    item.innerHTML = `<div class="conv-av">${conv.emoji}</div><div class="conv-info"><div class="conv-name-row"><span class="conv-name">${conv.name}</span><span class="conv-time">${lastMsg?formatPostTime(lastMsg.time):''}</span></div><div class="conv-preview">${escapeHTML(preview.length>38?preview.slice(0,38)+'…':preview)}</div></div>${conv.unread?`<div class="conv-unread">${conv.unread}</div>`:''}`;
    item.addEventListener('click', () => openChat(conv.id)); list.appendChild(item);
  });
}

function openChat(cid) {
  activeChatId = cid; const conv = conversations[cid]; if (!conv) return; conv.unread = 0;
  document.getElementById('msgNoChat').style.display = 'none'; document.getElementById('msgChatWindow').style.display = 'flex';
  document.getElementById('msgChatAv').textContent = conv.emoji; document.getElementById('msgChatName').textContent = conv.name;
  renderBubbles(cid); renderConvList(); updateMsgBadge(); setTimeout(() => document.getElementById('msgInput').focus(), 50);
}

function renderBubbles(cid) {
  const conv = conversations[cid]; if (!conv) return;
  const area = document.getElementById('msgBubbleArea'); area.innerHTML = '';
  if (!conv.messages.length) { area.innerHTML = '<div class="msg-start-hint">Say hello to '+conv.name+'! 👋</div>'; return; }
  let lastDate = '';
  conv.messages.forEach(m => {
    const d = m.time instanceof Date ? m.time : new Date(m.time);
    const dStr = d.toLocaleDateString('en-IN',{day:'numeric',month:'short'});
    if (dStr !== lastDate) { const sep = document.createElement('div'); sep.className='msg-date-sep'; sep.textContent=dStr; area.appendChild(sep); lastDate=dStr; }
    const row = document.createElement('div'); row.className = 'msg-bubble-row '+(m.from==='me'?'row-me':'row-them');
    const inner = document.getElementById('avatarInner'); const myAv = inner ? inner.innerHTML : '😊';
    row.innerHTML = `${m.from==='them'?`<div class="bubble-av">${conv.emoji}</div>`:''}<div class="msg-bubble ${m.from==='me'?'bubble-me':'bubble-them'}">${escapeHTML(m.text)}<div class="bubble-time">${d.toLocaleTimeString('en-IN',{hour:'2-digit',minute:'2-digit'})}</div></div>${m.from==='me'?`<div class="bubble-av">${myAv}</div>`:''}`;
    area.appendChild(row);
  });
  area.scrollTop = area.scrollHeight;
}

document.getElementById('msgSendBtn').addEventListener('click', sendFromChatInput);
document.getElementById('msgInput').addEventListener('keydown', e => { if (e.key==='Enter'&&!e.shiftKey) sendFromChatInput(); });
function sendFromChatInput() { const inp=document.getElementById('msgInput'); const text=inp.value.trim(); if (!text||!activeChatId) return; const conv=conversations[activeChatId]; if (!conv) return; inp.value=''; sendMessageTo(conv.name, conv.emoji, text); }

const emojiPicker = document.getElementById('msgEmojiPicker');
MSG_EMOJIS.forEach(em => { const btn=document.createElement('button'); btn.className='emoji-pick-btn'; btn.textContent=em; btn.onclick=()=>{ document.getElementById('msgInput').value+=em; document.getElementById('msgInput').focus(); emojiPicker.style.display='none'; }; emojiPicker.appendChild(btn); });
document.getElementById('msgEmojiBtn').addEventListener('click', e => { e.stopPropagation(); emojiPicker.style.display = emojiPicker.style.display==='none'?'flex':'none'; });
document.addEventListener('click', () => { emojiPicker.style.display = 'none'; });

function updateMsgBadge() {
  const total = Object.values(conversations).reduce((s,c) => s+(c.unread||0), 0);
  const btns = document.querySelectorAll('.nav-btn'), msgBtn = btns[2]; if (!msgBtn) return;
  const existing = msgBtn.querySelector('.nav-msg-badge');
  if (total > 0) { if (existing) existing.textContent = total; else { const badge = document.createElement('span'); badge.className='nav-msg-badge'; badge.textContent=total; msgBtn.appendChild(badge); } }
  else if (existing) existing.remove();
}

/* ════════════════════════════════════════════════════════════
   SHARED UTILITIES
════════════════════════════════════════════════════════════ */
function formatPostTime(dateInput) {
  const d = dateInput instanceof Date ? dateInput : new Date(dateInput);
  const diff = Math.floor((Date.now() - d) / 1000);
  if (diff < 60) return 'Just now'; if (diff < 3600) return Math.floor(diff/60)+' min ago';
  if (diff < 86400) return Math.floor(diff/3600)+' hr ago';
  return d.toLocaleDateString('en-IN', { day:'numeric', month:'short' });
}

function escapeHTML(str) { if (!str) return ''; return String(str).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;'); }

/* ════════════════════════════════════════════════════════════
   DEMO MODE
════════════════════════════════════════════════════════════ */
function loadDemoData() {
  _cachedSuggestions = [
    { id:1, username:'amit_shukla', display_name:'Amit Shukla', avatar_emoji:'😎', mutual_friends:3 },
    { id:2, username:'mansi_gupta', display_name:'Mansi Gupta', avatar_emoji:'😊', mutual_friends:5 },
    { id:3, username:'prateek_singh', display_name:'Prateek Singh', avatar_emoji:'🧑‍🎓', mutual_friends:2 },
    { id:4, username:'riya_sharma', display_name:'Riya Sharma', avatar_emoji:'😄', mutual_friends:4 },
    { id:5, username:'karan_mehta', display_name:'Karan Mehta', avatar_emoji:'🧑‍💻', mutual_friends:1 },
    { id:6, username:'divya_joshi', display_name:'Divya Joshi', avatar_emoji:'🌸', mutual_friends:2 },
  ];
  renderPeopleGrid(_cachedSuggestions);
  _cachedFriends = [
    { id:10, username:'raj_patel', display_name:'Raj Patel', avatar_emoji:'👨', mutual_friends:3 },
    { id:11, username:'neha_singh', display_name:'Neha Singh', avatar_emoji:'👩', mutual_friends:2 },
  ];
  renderMyFriends(_cachedFriends);
  document.getElementById('friendCount').textContent = '2';

  const list = document.getElementById('requestsList'); const noReq = document.getElementById('noRequests');
  list.innerHTML = `
    <div class="req-item" id="req_demo1"><div class="req-avatar" style="background:rgba(247,55,110,0.15);">😄</div><div class="req-info"><strong>Sneha Patel</strong><span>3 mutual friends</span><div class="req-btns"><button class="btn-confirm" onclick="demoAccept('req_demo1','Sneha Patel')">Confirm</button><button class="btn-decline" onclick="demoDecline('req_demo1')">Delete</button></div></div></div>
    <div class="req-item" id="req_demo2"><div class="req-avatar" style="background:rgba(167,139,250,0.15);">🧑‍💻</div><div class="req-info"><strong>Arjun Verma</strong><span>7 mutual friends</span><div class="req-btns"><button class="btn-confirm" onclick="demoAccept('req_demo2','Arjun Verma')">Confirm</button><button class="btn-decline" onclick="demoDecline('req_demo2')">Delete</button></div></div></div>`;
  noReq.style.display = 'none'; updateReqBadge(2);

  const demoFeed = document.getElementById('postFeed'); demoFeed.innerHTML = '';
  [
    { id:Date.now()+1, author_name:'Mansi Gupta', text:'Just joined BondUp! 🎉 So excited to connect with everyone here.', mood:'🥳 Excited', images:[], likes:5, liked_by_me:false, comments:2, created_at: new Date(Date.now()-120000).toISOString(), saved_by_me:false },
    { id:Date.now()+2, author_name:'Amit Shukla', text:'Beautiful day at MITS campus! ☀️', mood:'😊 Happy', images:[], likes:12, liked_by_me:true, comments:4, created_at: new Date(Date.now()-3600000).toISOString(), saved_by_me:false },
  ].forEach(p => demoFeed.appendChild(buildPostCard(p)));
}

function demoAccept(elemId, name) {
  document.getElementById(elemId)?.remove(); showToast(`You and ${name} are now friends! 🎉`, '🤝');
  const fc = document.getElementById('friendCount'); fc.textContent = parseInt(fc.textContent) + 1;
  const remaining = document.getElementById('requestsList').children.length;
  if (!remaining) document.getElementById('noRequests').style.display = 'block'; updateReqBadge(remaining);
}
function demoDecline(elemId) {
  document.getElementById(elemId)?.remove(); showToast('Request removed.', '🗑️');
  const remaining = document.getElementById('requestsList').children.length;
  if (!remaining) document.getElementById('noRequests').style.display = 'block'; updateReqBadge(remaining);
}

/* ── Init ── */
loadFeed();
checkServerStatus();
