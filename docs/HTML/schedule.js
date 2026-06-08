// ============================================================
//  schedule.js  –  Single source of truth for all timetables
//  IIT Dharwad Institute Bus Tracking System
// ============================================================

// ── Stop coordinates ──────────────────────────────────────
const STOP_COORDS = {
  "Hostel":        { lat: 15.483020, lng: 74.938931 },
  "Main Gate":     { lat: 15.487870, lng: 74.933658 },
  "NFSU":          { lat: 15.516959, lng: 74.926994 },
  "New Bus Stand": { lat: 15.469461, lng: 74.992749 },
  "Jubilee Circle":{ lat: 15.459100, lng: 75.007806 },
  "A1":            { lat: 15.484624, lng: 74.936509 },
  "A2":            { lat: 15.487266, lng: 74.935205 },
  "CLT":           { lat: 15.484073, lng: 74.934991 },
};

// ── Weekday-only stops (not served on weekends/holidays) ──
const WEEKDAY_ONLY_STOPS = new Set(["A1", "A2", "CLT", "Jubilee Circle"]);

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
  // Trip 1: Hostel → Main Gate → NFSU → Jubilee Circle  (8:00–8:40)
  _s("Hostel",         "08:00:00"),
  _s("Main Gate",      "08:05:00"),
  _s("NFSU",           "08:25:00"),
  _s("Jubilee Circle", "08:40:00"),
  // Trip 2: Jubilee Circle → New Bus Stand → Main Gate → Hostel  (8:41–9:20)
  // NOTE: Trip 2 start is 08:41 (1 min after Trip 1 end) so getUpcomingBuses
  // can distinguish the two Jubilee Circle entries at the trip boundary.
  _s("Jubilee Circle", "08:41:00"),
  _s("New Bus Stand",  "08:50:00"),
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

  // Lunch Shuttle Trip 1: Hostel → A1 → CLT → A2 → A1 → Hostel  (13:35–13:50)
  _s("Hostel",        "13:35:00"),
  _s("A1",            "13:38:00"),
  _s("CLT",           "13:40:00"),
  _s("A2",            "13:45:00"),
  _s("A1",            "13:47:00"),
  _s("Hostel",        "13:50:00"),

  // Lunch Shuttle Trip 2: Hostel → A1 → CLT → A2 → A1 → Hostel  (13:55–14:10)
  _s("Hostel",        "13:55:00"),
  _s("A1",            "13:58:00"),
  _s("CLT",           "14:00:00"),
  _s("A2",            "14:05:00"),
  _s("A1",            "14:07:00"),
  _s("Hostel",        "14:10:00"),

  // Lunch Shuttle Trip 3: Hostel → A1 → CLT → A2 → A1 → Hostel  (14:15–14:30)
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
// Uses Intl.DateTimeFormat formatToParts — spec-guaranteed, no toLocaleString
// parsing antipattern.
function getCurrentIST() {
  const now = new Date();
  const parts = new Intl.DateTimeFormat("en-US", {
    timeZone: "Asia/Kolkata",
    year: "numeric", month: "2-digit", day: "2-digit",
    hour: "2-digit", minute: "2-digit", second: "2-digit",
    hour12: false,
    weekday: "long"
  }).formatToParts(now);

  const get = (type) => parts.find(p => p.type === type).value;
  let hh = get("hour");
  // Intl hour12:false can return "24" for midnight — normalise to "00"
  if (hh === "24") hh = "00";
  const mm  = get("minute");
  const ss  = get("second");
  const mo  = get("month");
  const dd  = get("day");
  const day = get("weekday"); // e.g. "Monday"

  return {
    dayName: day,
    timeStr: `${hh}:${mm}:${ss}`,   // "HH:MM:SS"
    dateKey: `${mo}-${dd}`,          // "MM-DD" for holiday lookup
    nowMs:   now.getTime()           // raw ms for stale-check
  };
}

// ── Helper: convert HH:MM:SS → seconds ───────────────────
function timeToSeconds(t) {
  const [h, m, s] = t.split(":").map(Number);
  return h * 3600 + m * 60 + (s || 0);
}

// ── Helper: trim HH:MM:SS → HH:MM for display ────────────
function fmtTime(t) {
  return t.slice(0, 5); // "HH:MM:SS" → "HH:MM"
}

// ── Helper: pick correct schedule for today ───────────────
function getTodaySchedule() {
  const { dayName, dateKey } = getCurrentIST();
  const isWeekend = dayName === "Saturday" || dayName === "Sunday";
  const isHoliday = INSTITUTE_HOLIDAYS_2026.has(dateKey);
  return (isWeekend || isHoliday) ? WEEKEND_SCHEDULE : WEEKDAY_SCHEDULE;
}

// ── Helper: is today a weekend or institute holiday? ──────
function isTodayWeekendOrHoliday() {
  const { dayName, dateKey } = getCurrentIST();
  return dayName === "Saturday" || dayName === "Sunday" || INSTITUTE_HOLIDAYS_2026.has(dateKey);
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
// Trip-boundary safe: destination search stops before the next occurrence of
// the same stop name at an EARLIER or EQUAL time (different trip).
function getUpcomingBuses(schedule, stopName) {
  const { timeStr } = getCurrentIST();
  const nowSec = timeToSeconds(timeStr);
  const results = [];

  for (let i = 0; i < schedule.length; i++) {
    if (schedule[i].name !== stopName) continue;
    // FIX (bug #1 / off-by-one): use < so a bus due RIGHT NOW is still shown
    if (timeToSeconds(schedule[i].time) < nowSec) continue;

    const stopSec  = timeToSeconds(schedule[i].time);
    const minAway  = Math.max(0, Math.ceil((stopSec - nowSec) / 60));

    // Find destination: skip any immediate consecutive same-name entries that
    // are part of a turnaround (same time or later within the same trip),
    // but stop at a trip boundary (same name with an earlier or equal time).
    let j = i + 1;
    const iTimeSec = stopSec;
    while (j < schedule.length && schedule[j].name === stopName) {
      if (timeToSeconds(schedule[j].time) <= iTimeSec) break; // new trip
      j++;
    }
    const dest = (j < schedule.length && schedule[j].name !== stopName)
      ? schedule[j].name
      : "End of route";

    const hrs     = Math.floor(minAway / 60);
    const mins    = minAway % 60;
    const waitStr = hrs > 0
      ? (mins > 0 ? `${hrs}h ${mins}m` : `${hrs}h`)
      : `${mins} min`;
    const timeLabel = `in ${waitStr}`;

    results.push({ timeLabel, dest, scheduledTime: fmtTime(schedule[i].time) });
    if (results.length === 3) break;
  }
  return results;
}

// ── Helper: convert UTC timestamp → IST display string ───
function utcToISTString(utcStr) {
  const d = new Date(utcStr);
  return d.toLocaleString("en-IN", {
    timeZone: "Asia/Kolkata",
    hour12: false,
    year: "numeric", month: "short", day: "numeric",
    hour: "numeric", minute: "numeric", second: "numeric"
  });
}

// ── Helper: is GPS data stale? (uses raw ms, no midnight bug) ──
function isGPSStale(gpsUTCTimestamp, thresholdSeconds = 300) {
  const gpsMs = new Date(gpsUTCTimestamp).getTime();
  return (Date.now() - gpsMs) > (thresholdSeconds * 1000);
}

// ── Helper: return start time of TOMORROW's first bus (24h, HH:MM) ──
// Used in "service closed" messages so Friday night shows "10:00" (Saturday),
// not "08:00" (a weekday start that doesn't apply until Monday).
function getTomorrowStartTime() {
  const now     = new Date();
  // Compute tomorrow's date in IST using UTC offset (+5:30 = 330 min)
  const istMs   = now.getTime() + 330 * 60 * 1000;
  const istNow  = new Date(istMs);
  const tomorrow = new Date(istNow);
  tomorrow.setUTCDate(tomorrow.getUTCDate() + 1);

  const tomorrowParts = new Intl.DateTimeFormat("en-US", {
    timeZone: "Asia/Kolkata",
    weekday: "long", month: "2-digit", day: "2-digit"
  }).formatToParts(new Date(tomorrow.getTime() - 330 * 60 * 1000)); // back to UTC for Intl

  // Re-derive from a real Date so Intl handles DST / leap days correctly
  const tDate = new Date(now);
  tDate.setDate(tDate.getDate() + 1);
  const tParts = new Intl.DateTimeFormat("en-US", {
    timeZone: "Asia/Kolkata",
    weekday: "long", month: "2-digit", day: "2-digit"
  }).formatToParts(tDate);

  const tDay     = tParts.find(p => p.type === "weekday").value;
  const tMonth   = tParts.find(p => p.type === "month").value;
  const tDayNum  = tParts.find(p => p.type === "day").value;
  const tDateKey = `${tMonth}-${tDayNum}`;

  const isWeekend = tDay === "Saturday" || tDay === "Sunday";
  const isHoliday = INSTITUTE_HOLIDAYS_2026.has(tDateKey);
  // FIX: return 24-hour format consistent with all other schedule times
  return (isWeekend || isHoliday) ? "10:00" : "08:00";
}

// ── Shared zoom helpers ───────────────────────────────────
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
