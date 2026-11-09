.pragma library

// The date picker's arithmetic (DatePickerPopup, DateField), kept apart so the tests can check it:
// months 1-12, days 1-31, weeks starting on Sunday. Values go in and out as the text the
// screens already keep: "2026-09-01", "2026-09-01 7:00 PM" (or 19:00) and "7:00 PM" / "19:00".

function isLeap(y) { return (y % 4 === 0 && y % 100 !== 0) || y % 400 === 0 }

function daysInMonth(y, m) {
    return [31, isLeap(y) ? 29 : 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31][m - 1]
}

// The same day in another month or year, or that month's last day when it has fewer
// (Jan 31 -> Feb 28 or 29; Feb 29 -> the next year's Feb 28).
function clampDay(y, m, d) { return Math.max(1, Math.min(d, daysInMonth(y, m))) }

// 0 Sunday ... 6 Saturday, for the first of the month: how many blanks before day 1.
function firstWeekday(y, m) { return new Date(y, m - 1, 1).getDay() }

// A month moved by `by` (-1, +1): {year, month}.
function addMonths(y, m, by) {
    const i = y * 12 + (m - 1) + by
    return { year: Math.floor(i / 12), month: i - Math.floor(i / 12) * 12 + 1 }
}

function pad(n) { return (n < 10 ? "0" : "") + n }

function iso(y, m, d) { return String(y).padStart(4, "0") + "-" + pad(m) + "-" + pad(d) }

// "2026-09-01" (and what may follow it): {year, month, day}, or null.
function parseDate(text) {
    const t = String(text ?? "").trim().match(/^(\d{4})-(\d{1,2})-(\d{1,2})/)
    if (!t)
        return null
    const y = Number(t[1]), m = Number(t[2]), d = Number(t[3])
    if (m < 1 || m > 12 || d < 1 || d > daysInMonth(y, m))
        return null
    return { year: y, month: m, day: d }
}

// "7:00 PM", "7 pm", "19:00": {hour 0-23, minute}, or null.
function parseTime(text) {
    const t = String(text ?? "").trim().match(/(\d{1,2})(?::(\d{2}))?\s*([AaPp])?\.?\s*[Mm]?\.?$/)
    if (!t)
        return null
    let h = Number(t[1])
    const mi = t[2] === undefined ? 0 : Number(t[2])
    if (mi > 59)
        return null
    if (t[3]) {
        if (h < 1 || h > 12)
            return null
        const pm = t[3] === "p" || t[3] === "P"
        h = h % 12 + (pm ? 12 : 0)
    } else if (h > 23 || t[2] === undefined) {
        return null
    }
    return { hour: h, minute: mi }
}

// "7:05 PM" (as the store keeps times: English AM/PM), or "19:05" when twentyFour.
function timeText(h, mi, twentyFour) {
    if (twentyFour)
        return pad(h) + ":" + pad(mi)
    return (h % 12 === 0 ? 12 : h % 12) + ":" + pad(mi) + (h < 12 ? " AM" : " PM")
}

// What the picker gives back: "2026-09-01", "2026-09-01 7:00 PM" or "7:00 PM".
function valueText(mode, y, m, d, h, mi, twentyFour) {
    if (mode === "time")
        return timeText(h, mi, twentyFour)
    if (mode === "datetime")
        return iso(y, m, d) + " " + timeText(h, mi, twentyFour)
    return iso(y, m, d)
}

// The days as the grid shows them: 42 cells (6 weeks from Sunday), 0 for a blank.
function monthCells(y, m) {
    const first = firstWeekday(y, m), n = daysInMonth(y, m), out = []
    for (let i = 0; i < 42; ++i) {
        const d = i - first + 1
        out.push(d >= 1 && d <= n ? d : 0)
    }
    return out
}
