// ============================================================
//  schedule.js  –  Single source of truth for all timetables
//  IIT Dharwad Institute Bus Tracking System
// ============================================================

// ── Stop coordinates ──────────────────────────────────────
const STOP_COORDS = {
  "Hostel":       { lat: 15.483020, lng: 74.938931 },
  "Main Gate":    { lat: 15.487870, lng: 74.933658 },
  "NFSU":         { lat: 15.516959, lng: 74.926994 },
  "New Bus Stand":{ lat: 15.469461, lng: 74.992749 },
  "Jubilee Circle":{ lat: 15.459100, lng: 75.007806 },
  "A1":           { lat: 15.484624, lng: 74.936509 },
  "A2":           { lat: 15.487266, lng: 74.935205 },
  "CLT":          { lat: 15.484073, lng: 74.934991 },
};

// ── 2026 Institute Holidays (from official list) ──────────
// Format: "MM-DD"
const INSTITUTE_HOLIDAYS_2026 = new Set([
  "01-14", // Makar Sankranti
  "01-26", // Republic Day
  "03-04", // Holi
  "03-19", // Chaitra Sukladi / Gudi Padava / Yugadi / Cheti Chand
  "03-21", // Id-ul-Fitr*
  "03-31", // Mahavir Jayanti
  "04-03", // Good Friday
  "04-14", // Ambedkar Jayanti
  "05-01", // Buddha Purnima
  "05-27", // Id-ul-Zuha (Bakrid)*
  "06-26", // Muharram*
  "08-15", // Independence Day
  "08-26", // Milad-un-Nabi*
  "09-14", // Ganesh Chaturthi
  "10-02", // Gandhi Jayanti
  "10-19", // Mahanavami
  "10-20", // Vijaya Dashami
  "11-01", // Karnataka Rajyotsava
  "11-08", // Naraka Chaturdashi
  "11-24", // Guru Nanak Jayanti
  "12-25", // Christmas Day
]);

// ── Weekday timetable (Mon–Fri, non-holiday) ─────────────
// Derived from official schedule images.
// Note: "Bhoopali/High-court Main-Gate" = NFSU
//       "Hostel-PC" = Hostel
//       "New-Bus-Stand Dharwad" = New Bus Stand

function _s(name, time) {
  const c = STOP_COORDS[name];
  return { name, time, latitude: c.lat, longitude: c.lng };
}

const WEEKDAY_SCHEDULE = [
  // Trip 1: Hostel → Main Gate → NFSU → New Bus Stand  (8:00–8:40)
  _s("Hostel",        "08:00:00"),
  _s("Main Gate",     "08:05:00"),
  _s("NFSU",          "08:25:00"),
  _s("New Bus Stand", "08:40:00"),
  // Trip 2: New Bus Stand → Jubilee Circle → Main Gate → Hostel  (8:50–9:20)
  _s("New Bus Stand",  "08:50:00"),
  _s("Jubilee Circle", "09:05:00"),
  _s("Main Gate",      "09:10:00"),
  _s("Hostel",         "09:20:00"),

  // Trip 3: Hostel → Main Gate → NFSU → New Bus Stand  (12:10–12:45)
  _s("Hostel",        "12:10:00"),
  _s("Main Gate",     "12:15:00"),
  _s("NFSU",          "12:30:00"),
  _s("New Bus Stand", "12:45:00"),
  // Trip 4: New Bus Stand → NFSU → Main Gate → Hostel  (12:50–13:30)
  _s("New Bus Stand", "12:50:00"),
  _s("NFSU",          "13:15:00"),
  _s("Main Gate",     "13:25:00"),
  _s("Hostel",        "13:30:00"),

  // Lunch Shuttle Trip 1: Hostel → A1 → CLT → A2/Admin  (13:35–13:45)
  _s("Hostel",        "13:35:00"),
  _s("A1",            "13:38:00"),
  _s("CLT",           "13:40:00"),
  _s("A2",            "13:45:00"),
  _s("A1",            "13:47:00"),
  _s("Hostel",        "13:50:00"),

  // Lunch Shuttle Trip 2: Hostel → A1 → CLT → A2  (13:55–14:10)
  _s("Hostel",        "13:55:00"),
  _s("A1",            "13:58:00"),
  _s("CLT",           "14:00:00"),
  _s("A2",            "14:05:00"),
  _s("A1",            "14:07:00"),
  _s("Hostel",        "14:10:00"),

  // Lunch Shuttle Trip 3: Hostel → A1 → CLT → A2  (14:15–14:30)
  _s("Hostel",        "14:15:00"),
  _s("A1",            "14:18:00"),
  _s("CLT",           "14:20:00"),
  _s("A2",            "14:25:00"),
  _s("A1",            "14:27:00"),
  _s("Hostel",        "14:30:00"),

  // Trip 5: Hostel → Main Gate → NFSU → New Bus Stand  (14:50–15:25)
  _s("Hostel",        "14:50:00"),
  _s("Main Gate",     "14:55:00"),
  _s("NFSU",          "15:10:00"),
  _s("New Bus Stand", "15:25:00"),
  // Trip 6: New Bus Stand → NFSU → Main Gate → Hostel  (15:30–16:10)
  _s("New Bus Stand", "15:30:00"),
  _s("NFSU",          "15:55:00"),
  _s("Main Gate",     "16:05:00"),
  _s("Hostel",        "16:10:00"),

  // Trip 7: Hostel → Main Gate → NFSU → New Bus Stand  (18:25–19:00)
  _s("Hostel",        "18:25:00"),
  _s("Main Gate",     "18:30:00"),
  _s("NFSU",          "18:45:00"),
  _s("New Bus Stand", "19:00:00"),
  // Trip 8: New Bus Stand → NFSU → Main Gate → Hostel  (19:05–19:45)
  _s("New Bus Stand", "19:05:00"),
  _s("NFSU",          "19:30:00"),
  _s("Main Gate",     "19:40:00"),
  _s("Hostel",        "19:45:00"),

  // Trip 9: Hostel → Main Gate → NFSU → New Bus Stand  (19:50–20:25)
  _s("Hostel",        "19:50:00"),
  _s("Main Gate",     "19:55:00"),
  _s("NFSU",          "20:10:00"),
  _s("New Bus Stand", "20:25:00"),
  // Trip 10: New Bus Stand → NFSU → Main Gate → Hostel  (20:30–21:10)
  _s("New Bus Stand", "20:30:00"),
  _s("NFSU",          "20:55:00"),
  _s("Main Gate",     "21:05:00"),
  _s("Hostel",        "21:10:00"),
];

// ── Weekend / Holiday timetable (Sat, Sun, Institute Holidays) ───
const WEEKEND_SCHEDULE = [
  // Trip 1: Hostel → Main Gate → NFSU → New Bus Stand  (10:00–10:40)
  _s("Hostel",        "10:00:00"),
  _s("Main Gate",     "10:05:00"),
  _s("NFSU",          "10:20:00"),
  _s("New Bus Stand", "10:40:00"),
  // Trip 2: New Bus Stand → NFSU → Main Gate → Hostel  (10:50–11:30)
  _s("New Bus Stand", "10:50:00"),
  _s("NFSU",          "11:15:00"),
  _s("Main Gate",     "11:25:00"),
  _s("Hostel",        "11:30:00"),

  // Trip 3: Hostel → Main Gate → NFSU → New Bus Stand  (12:30–13:05)
  _s("Hostel",        "12:30:00"),
  _s("Main Gate",     "12:35:00"),
  _s("NFSU",          "12:50:00"),
  _s("New Bus Stand", "13:05:00"),
  // Trip 4: New Bus Stand → NFSU → Main Gate → Hostel  (13:15–13:55)
  _s("New Bus Stand", "13:15:00"),
  _s("NFSU",          "13:35:00"),
  _s("Main Gate",     "13:50:00"),
  _s("Hostel",        "13:55:00"),

  // Trip 5: Hostel → Main Gate → NFSU → New Bus Stand  (17:30–18:05)
  _s("Hostel",        "17:30:00"),
  _s("Main Gate",     "17:35:00"),
  _s("NFSU",          "17:50:00"),
  _s("New Bus Stand", "18:05:00"),
  // Trip 6: New Bus Stand → NFSU → Main Gate → Hostel  (18:10–18:45)
  _s("New Bus Stand", "18:10:00"),
  _s("NFSU",          "18:30:00"),
  _s("Main Gate",     "18:40:00"),
  _s("Hostel",        "18:45:00"),

  // Trip 7: Hostel → Main Gate → NFSU → New Bus Stand  (19:35–20:10)
  _s("Hostel",        "19:35:00"),
  _s("Main Gate",     "19:40:00"),
  _s("NFSU",          "19:55:00"),
  _s("New Bus Stand", "20:10:00"),
  // Trip 8: New Bus Stand → NFSU → Main Gate → Hostel  (20:15–20:50)
  _s("New Bus Stand", "20:15:00"),
  _s("NFSU",          "20:35:00"),
  _s("Main Gate",     "20:45:00"),
  _s("Hostel",        "20:50:00"),
];

// ── Helper: get current IST time components ───────────────
function getCurrentIST() {
  const now = new Date();
  const ist = new Date(now.toLocaleString("en-US", { timeZone: "Asia/Kolkata" }));
  const days = ["Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"];
  const hh = ist.getHours().toString().padStart(2,'0');
  const mm = ist.getMinutes().toString().padStart(2,'0');
  const ss = ist.getSeconds().toString().padStart(2,'0');
  const mo = (ist.getMonth()+1).toString().padStart(2,'0');
  const dd = ist.getDate().toString().padStart(2,'0');
  return {
    dayName:  days[ist.getDay()],
    timeStr:  `${hh}:${mm}:${ss}`,   // "HH:MM:SS"
    dateKey:  `${mo}-${dd}`,          // "MM-DD" for holiday lookup
    dateObj:  ist,
    nowMs:    now.getTime()           // raw ms for stale-check
  };
}

// ── Helper: convert HH:MM:SS → seconds ───────────────────
function timeToSeconds(t) {
  const [h,m,s] = t.split(":").map(Number);
  return h * 3600 + m * 60 + (s || 0);
}

// ── Helper: pick correct schedule for today ───────────────
function getTodaySchedule() {
  const { dayName, dateKey } = getCurrentIST();
  const isWeekend = dayName === "Saturday" || dayName === "Sunday";
  const isHoliday = INSTITUTE_HOLIDAYS_2026.has(dateKey);
  return (isWeekend || isHoliday) ? WEEKEND_SCHEDULE : WEEKDAY_SCHEDULE;
}

// ── Helper: first time in schedule ───────────────────────
function scheduleFirstTime(schedule) {
  return schedule[0].time;
}

// ── Helper: last time in schedule ────────────────────────
function scheduleLastTime(schedule) {
  return schedule[schedule.length - 1].time;
}

// ── Helper: get up to 3 upcoming buses at a named stop ───
// Uses real wall-clock IST time, not GPS timestamp.
// Correctly handles consecutive same-name entries.
function getUpcomingBuses(schedule, stopName) {
  const { timeStr } = getCurrentIST();
  const nowSec = timeToSeconds(timeStr);
  const results = [];

  for (let i = 0; i < schedule.length; i++) {
    if (schedule[i].name !== stopName) continue;
    if (timeToSeconds(schedule[i].time) <= nowSec) continue;

    const minAway = Math.ceil((timeToSeconds(schedule[i].time) - nowSec) / 60);

    // Find direction: skip consecutive entries with same stop name
    let j = i + 1;
    while (j < schedule.length && schedule[j].name === stopName) j++;
    const dest = j < schedule.length ? schedule[j].name : "End of route";

    const hrs  = Math.floor(minAway / 60);
    const mins = minAway % 60;
    const timeLabel = hrs > 0 ? `${hrs}h ${mins}m` : `${mins} min`;

    results.push({ timeLabel, dest, scheduledTime: schedule[i].time });
    if (results.length === 3) break;
  }
  return results;
}

// ── Helper: convert UTC timestamp → IST display string ───
function utcToISTString(utcStr) {
  const d = new Date(utcStr);
  return d.toLocaleString('en-IN', {
    timeZone: 'Asia/Kolkata',
    hour12: false,
    year: 'numeric', month: 'short', day: 'numeric',
    hour: 'numeric', minute: 'numeric', second: 'numeric'
  });
}

// ── Helper: is GPS data stale? (uses raw ms, no midnight bug) ──
function isGPSStale(gpsUTCTimestamp, thresholdSeconds = 300) {
  const gpsMs  = new Date(gpsUTCTimestamp).getTime();
  const nowMs  = Date.now();
  return (nowMs - gpsMs) > (thresholdSeconds * 1000);
}

// ── Fix 2: return the correct start time string for TOMORROW's schedule ──
// Used in "service closed" messages so Friday night shows "10:00 AM" (Saturday),
// not "08:00 AM" (a weekday start that doesn't apply until Monday).
function getTomorrowStartTime() {
  const now  = new Date();
  const istNow = new Date(now.toLocaleString("en-US", { timeZone: "Asia/Kolkata" }));
  const tomorrow = new Date(istNow);
  tomorrow.setDate(tomorrow.getDate() + 1);
  const days = ["Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"];
  const dayName = days[tomorrow.getDay()];
  const mo = (tomorrow.getMonth()+1).toString().padStart(2,'0');
  const dd = tomorrow.getDate().toString().padStart(2,'0');
  const dateKey = `${mo}-${dd}`;
  const isWeekend = dayName === "Saturday" || dayName === "Sunday";
  const isHoliday = INSTITUTE_HOLIDAYS_2026.has(dateKey);
  return (isWeekend || isHoliday) ? "10:00 AM" : "08:00 AM";
}

// ── Fix 9: shared zoom helpers (moved from per-page duplication) ──────────
// Stop pages use slightly wider thresholds (0.37/0.75…) vs Bus_Tracking
// (0.35/0.70…); a single unified table satisfies both cases adequately.
function calculateDistance(lat1, lon1, lat2, lon2) {
  const R = 6371, dLat = (lat2-lat1)*Math.PI/180, dLon = (lon2-lon1)*Math.PI/180;
  const a = Math.sin(dLat/2)**2 + Math.cos(lat1*Math.PI/180)*Math.cos(lat2*Math.PI/180)*Math.sin(dLon/2)**2;
  return R * 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1-a));
}
function getZoomLevel(dist) {
  if (dist <= 0.37) return 17; if (dist <= 0.75) return 16;
  if (dist <= 1.50) return 15; if (dist <= 3.00) return 14;
  if (dist <= 6.00) return 13; if (dist <= 12.0) return 12; return 11;
}

// ── Fading trail opacities (oldest → newest) ─────────────
const TRAIL_OPACITIES = [0.15, 0.30, 0.50, 0.75];
