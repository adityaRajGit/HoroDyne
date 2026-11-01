#pragma once
// GitHub data for one user: PRs, Actions runs and commits. Fetched only when the GitHub app opens
// or refresh is tapped on the face. Kept in RAM; a reboot starts with no data.
#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include <ArduinoJson.h>

enum GhState : uint8_t { GH_NONE, GH_PASS, GH_RUN, GH_FAIL };
enum GhError : uint8_t { GH_OK, GH_NO_WIFI, GH_BAD_TOKEN, GH_RATE_LIMIT, GH_FETCH_FAILED };

struct GhPr {
    uint32_t number;
    char     title[56], repo[24];
    time_t   when;              // last update, or merge time
    GhState  ci;
    uint16_t checks;            // check count on the last commit
    uint32_t added, removed, files;
    uint16_t approvals, reviewers;   // reviewers = approved + still requested
};

struct GhJob { char name[24]; GhState state; uint16_t secs; };

struct GhRun {
    char     name[32], repo[24], branch[24];
    uint32_t number;
    GhState  state;
    time_t   when;
    uint8_t  jobCount;
    GhJob    jobs[6];
    uint8_t  jobsDone() const { uint8_t n = 0; for (uint8_t i = 0; i < jobCount; i++) n += jobs[i].state == GH_PASS; return n; }
};

struct GhCommit { char sha[8], message[56], repo[24]; time_t when; };

struct GitHub {
    time_t   fetchedAt;         // 0 = never
    GhError  error;             // result of the last attempt; older data stays usable
    uint16_t openCount, mergedCount, reviewCount;
    GhPr     open[5], merged[5];
    uint8_t  openShown, mergedShown;
    GhRun    runs[3];
    uint8_t  runCount;
    uint16_t days[14];          // contributions per day, today last
    uint16_t weekTotal, streak;
    GhCommit commits[5];
    uint8_t  commitCount;
};

extern GitHub gh;

bool    ghParseData(JsonDocument &doc, GitHub &out);     // GraphQL reply
void    ghParseCommits(JsonDocument &doc, GitHub &out);  // REST commit search reply
time_t  ghParseTime(const char *iso);                    // "2026-11-01T07:30:00Z", 0 if malformed
bool    ghValidLogin(const char *login);                 // letters, digits and single hyphens
const GhRun *ghHeadRun(const GitHub &g);                 // running run first, else the newest
void    ghAge(char *out, size_t n, time_t when, time_t now);  // "2M", "3H", "1D"
GhError ghFetch();                                       // Wi-Fi must be up; fills gh
