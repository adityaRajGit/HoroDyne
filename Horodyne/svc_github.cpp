#include "svc_github.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

// GraphQL replies nest 13 levels deep (run -> check suite -> check run); ArduinoJson stops at 10.
static const int GH_JSON_DEPTH = 16;

GitHub gh = {};

// The watch fonts are ASCII only: anything else would draw as nothing.
static void copyAscii(char *out, size_t n, const char *s) {
    size_t o = 0;
    if (!s) s = "";
    while (*s && *s != '\n' && o + 1 < n) {
        unsigned char c = *s;
        if (c < 0x80) { out[o++] = c; s++; continue; }
        out[o++] = '?';                                  // one mark per UTF-8 character
        for (s++; (*s & 0xC0) == 0x80; s++) {}
    }
    out[o] = 0;
}

time_t ghParseTime(const char *t) {
    int y, m, d, hh, mm, ss;
    if (!t || sscanf(t, "%d-%d-%dT%d:%d:%d", &y, &m, &d, &hh, &mm, &ss) != 6) return 0;
    y -= m <= 2;                                         // days from civil (Howard Hinnant)
    int era = y / 400, yoe = y - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long days = era * 146097L + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
    return (time_t)(days * 86400LL + hh * 3600 + mm * 60 + ss);
}

bool ghValidLogin(const char *s) {
    if (!s || !*s || *s == '-' || strlen(s) > 39) return false;
    for (; *s; s++) if (!isalnum((unsigned char)*s) && *s != '-') return false;
    return true;
}

static GhState rollupState(const char *s) {
    if (!s) return GH_NONE;
    if (!strcmp(s, "SUCCESS")) return GH_PASS;
    if (!strcmp(s, "PENDING") || !strcmp(s, "EXPECTED")) return GH_RUN;
    return GH_FAIL;
}

// Check suites and check runs share these fields.
static GhState checkState(const char *status, const char *conclusion) {
    if (!status) return GH_NONE;
    if (strcmp(status, "COMPLETED")) return !strcmp(status, "IN_PROGRESS") ? GH_RUN : GH_NONE;
    if (!conclusion) return GH_NONE;
    return !strcmp(conclusion, "SUCCESS") || !strcmp(conclusion, "NEUTRAL") || !strcmp(conclusion, "SKIPPED") ? GH_PASS : GH_FAIL;
}

static uint8_t readPrs(JsonArray nodes, GhPr *out, uint8_t max) {
    uint8_t n = 0;
    for (JsonObject o : nodes) {
        if (n == max) break;
        GhPr &p = out[n++];
        p.number = o["number"] | 0;
        copyAscii(p.title, sizeof(p.title), o["title"]);
        copyAscii(p.repo, sizeof(p.repo), o["repository"]["name"]);
        const char *when = o["mergedAt"] | (const char *)nullptr;
        p.when = ghParseTime(when ? when : o["updatedAt"].as<const char *>());
        JsonObject roll = o["commits"]["nodes"][0]["commit"]["statusCheckRollup"];
        p.ci = roll.isNull() ? GH_NONE : rollupState(roll["state"]);
        p.checks = roll["contexts"]["totalCount"] | 0;
        p.added = o["additions"] | 0;
        p.removed = o["deletions"] | 0;
        p.files = o["changedFiles"] | 0;
        p.approvals = o["reviews"]["totalCount"] | 0;
        p.reviewers = p.approvals + (o["reviewRequests"]["totalCount"] | 0);
    }
    return n;
}

static void readDays(JsonArray weeks, GitHub &g) {
    memset(g.days, 0, sizeof(g.days));
    for (JsonObject w : weeks)
        for (JsonObject d : JsonArray(w["contributionDays"])) {   // oldest first: shift each one in
            memmove(g.days, g.days + 1, 13 * sizeof(g.days[0]));
            g.days[13] = d["contributionCount"] | 0;
        }
    g.weekTotal = 0;
    for (int i = 7; i < 14; i++) g.weekTotal += g.days[i];
    g.streak = 0;
    int i = g.days[13] ? 13 : 12;                        // an empty today doesn't break the streak
    for (; i >= 0 && g.days[i]; i--) g.streak++;
}

static void readRuns(JsonArray repos, GitHub &g) {
    g.runCount = 0;
    for (JsonObject repo : repos) {
        if (g.runCount == 3) break;
        JsonObject suite = repo["defaultBranchRef"]["target"]["checkSuites"]["nodes"][0];
        if (suite.isNull()) continue;
        GhRun &r = g.runs[g.runCount++];
        const char *wf = suite["workflowRun"]["workflow"]["name"] | "CHECKS";
        copyAscii(r.name, sizeof(r.name), wf);
        copyAscii(r.repo, sizeof(r.repo), repo["name"]);
        copyAscii(r.branch, sizeof(r.branch), repo["defaultBranchRef"]["name"]);
        r.number = suite["workflowRun"]["runNumber"] | 0;
        r.state = checkState(suite["status"], suite["conclusion"]);
        r.when = ghParseTime(suite["updatedAt"]);
        r.jobCount = 0;
        for (JsonObject c : JsonArray(suite["checkRuns"]["nodes"])) {
            if (r.jobCount == 6) break;
            GhJob &j = r.jobs[r.jobCount++];
            copyAscii(j.name, sizeof(j.name), c["name"]);
            j.state = checkState(c["status"], c["conclusion"]);
            time_t a = ghParseTime(c["startedAt"]), b = ghParseTime(c["completedAt"]);
            j.secs = a && b > a ? (uint16_t)(b - a) : 0;
        }
    }
}

bool ghParseData(JsonDocument &doc, GitHub &g) {
    JsonObject d = doc["data"];
    if (d.isNull() || d["open"].isNull()) return false;
    g.openCount = d["open"]["issueCount"] | 0;
    g.mergedCount = d["merged"]["issueCount"] | 0;
    g.reviewCount = d["review"]["issueCount"] | 0;
    g.openShown = readPrs(d["open"]["nodes"], g.open, 5);
    g.mergedShown = readPrs(d["merged"]["nodes"], g.merged, 5);
    readDays(d["user"]["contributionsCollection"]["contributionCalendar"]["weeks"], g);
    readRuns(d["user"]["repositories"]["nodes"], g);
    return true;
}

void ghParseCommits(JsonDocument &doc, GitHub &g) {
    g.commitCount = 0;
    for (JsonObject o : JsonArray(doc["items"])) {
        if (g.commitCount == 5) break;
        GhCommit &c = g.commits[g.commitCount++];
        strncpy(c.sha, o["sha"] | "", 7);
        c.sha[7] = 0;
        copyAscii(c.message, sizeof(c.message), o["commit"]["message"]);
        copyAscii(c.repo, sizeof(c.repo), o["repository"]["name"]);
        c.when = ghParseTime(o["commit"]["author"]["date"]);
    }
}

const GhRun *ghHeadRun(const GitHub &g) {
    for (uint8_t i = 0; i < g.runCount; i++) if (g.runs[i].state == GH_RUN) return &g.runs[i];
    const GhRun *best = nullptr;
    for (uint8_t i = 0; i < g.runCount; i++) if (!best || g.runs[i].when > best->when) best = &g.runs[i];
    return best;
}

void ghAge(char *out, size_t n, time_t when, time_t now) {
    long s = (long)(now - when);
    if (s < 60)         strncpy(out, "NOW", n);
    else if (s < 3600)  snprintf(out, n, "%ldM", s / 60);
    else if (s < 86400) snprintf(out, n, "%ldH", s / 3600);
    else                snprintf(out, n, "%ldD", s / 86400);
}

// ----- network ------------------------------------------------------------------------
#ifndef GH_NO_NET
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "gh_ca.h"
#include <esp_heap_caps.h>
#include "svc_settings.h"

static const char QUERY[] =
    "query($o:String!,$m:String!,$r:String!,$u:String!){"
    "open:search(query:$o,type:ISSUE,first:5){issueCount nodes{...P}}"
    "merged:search(query:$m,type:ISSUE,first:5){issueCount nodes{...P}}"
    "review:search(query:$r,type:ISSUE,first:1){issueCount}"
    "user(login:$u){"
    "contributionsCollection{contributionCalendar{weeks{contributionDays{contributionCount}}}}"
    "repositories(first:3,orderBy:{field:PUSHED_AT,direction:DESC}){nodes{name defaultBranchRef{name target{...on Commit{"
    "checkSuites(last:1){nodes{status conclusion updatedAt workflowRun{runNumber workflow{name}}"
    "checkRuns(first:6){nodes{name status conclusion startedAt completedAt}}}}}}}}}}}"
    "fragment P on PullRequest{number title updatedAt mergedAt additions deletions changedFiles repository{name}"
    "commits(last:1){nodes{commit{statusCheckRollup{state contexts{totalCount}}}}}"
    "reviews(states:APPROVED){totalCount} reviewRequests{totalCount}}";

static GhError codeError(int code) {
    if (code == 401) return GH_BAD_TOKEN;
    if (code == 403 || code == 429) return GH_RATE_LIMIT;
    return GH_FETCH_FAILED;
}

// One authenticated request. The token travels only over a verified TLS connection.
static GhError request(const char *url, const char *body, JsonDocument &doc, JsonDocument *filter) {
    WiFiClientSecure client;
    client.setCACert(GH_CA);
    HTTPClient http;
    http.setTimeout(15000);
    if (!http.begin(client, url)) return GH_FETCH_FAILED;
    http.addHeader("Authorization", String("Bearer ") + settings.ghToken);
    http.addHeader("User-Agent", "Horodyne-watch");
    http.addHeader("Accept", "application/vnd.github+json");
    int code = body ? http.POST((uint8_t *)body, strlen(body)) : http.GET();
    GhError r = GH_OK;
    Serial.printf("github: %s -> %d, %d bytes, internal heap %u, largest %u\n", body ? "POST" : "GET", code, http.getSize(),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    if (code != 200) {
        char tls[120] = "";
        client.lastError(tls, sizeof(tls));
        Serial.printf("github: failed: %s / %s\n", http.errorToString(code).c_str(), tls);
        r = codeError(code);
    } else {
        // Parsing straight off the TLS stream stopped partway through replies of 10 KB and more
        // (IncompleteInput, build 30). Take the whole body first; HTTPClient decodes the chunks.
        String reply = http.getString();
        DeserializationError e = filter
            ? deserializeJson(doc, reply, DeserializationOption::Filter(*filter), DeserializationOption::NestingLimit(GH_JSON_DEPTH))
            : deserializeJson(doc, reply, DeserializationOption::NestingLimit(GH_JSON_DEPTH));
        Serial.printf("github: body %u bytes, json %s\n", reply.length(), e.c_str());
        if (e) r = GH_FETCH_FAILED;
    }
    http.end();
    return r;
}

GhError ghFetch() {
    const char *u = settings.ghUser;
    if (!ghValidLogin(u) || !settings.ghToken[0]) return gh.error = GH_BAD_TOKEN;

    JsonDocument vars;
    vars["query"] = QUERY;
    vars["variables"]["o"] = String("is:pr is:open archived:false sort:updated author:") + u;
    vars["variables"]["m"] = String("is:pr is:merged sort:updated author:") + u;
    vars["variables"]["r"] = String("is:pr is:open review-requested:") + u;
    vars["variables"]["u"] = u;
    String body;
    serializeJson(vars, body);

    JsonDocument doc;
    GhError r = request("https://api.github.com/graphql", body.c_str(), doc, nullptr);
    if (r == GH_OK && !ghParseData(doc, gh)) {
        Serial.printf("github: reply error: %s\n", (const char *)(doc["errors"][0]["message"] | "no data"));
        r = GH_FETCH_FAILED;
    }
    if (r != GH_OK) return gh.error = r;

    JsonDocument filter;
    filter["items"][0]["sha"] = true;
    filter["items"][0]["commit"]["message"] = true;
    filter["items"][0]["commit"]["author"]["date"] = true;
    filter["items"][0]["repository"]["name"] = true;
    doc.clear();
    String url = String("https://api.github.com/search/commits?sort=author-date&order=desc&per_page=5&q=author:") + u;
    // PRs and runs are already in: a failed commit list keeps the old one rather than failing the sync.
    if (request(url.c_str(), nullptr, doc, &filter) == GH_OK) ghParseCommits(doc, gh);

    gh.fetchedAt = time(nullptr);
    return gh.error = GH_OK;
}
#endif
