// PC check of the GitHub reply parser. Build: see run_tests.sh.
#define GH_NO_NET
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../Horodyne/svc_github.cpp"

static const char REPLY[] = R"({"data":{
 "open":{"issueCount":7,"nodes":[
  {"number":482,"title":"Fix battery é drift","updatedAt":"2026-11-01T07:30:00Z","mergedAt":null,"additions":128,"deletions":34,"changedFiles":6,
   "repository":{"name":"horodyne"},"commits":{"nodes":[{"commit":{"statusCheckRollup":{"state":"SUCCESS","contexts":{"totalCount":5}}}}]},
   "reviews":{"totalCount":1},"reviewRequests":{"totalCount":1}},
  {"number":61,"title":"No checks","updatedAt":"2026-10-30T00:00:00Z","additions":1,"deletions":0,"changedFiles":1,
   "repository":{"name":"valorax"},"commits":{"nodes":[{"commit":{"statusCheckRollup":null}}]},"reviews":{"totalCount":0},"reviewRequests":{"totalCount":0}},
  {"number":9,"title":"Pending","updatedAt":"2026-10-30T00:00:00Z","repository":{"name":"x"},"commits":{"nodes":[{"commit":{"statusCheckRollup":{"state":"PENDING","contexts":{"totalCount":2}}}}]}},
  {"number":8,"title":"Red","updatedAt":"2026-10-30T00:00:00Z","repository":{"name":"x"},"commits":{"nodes":[{"commit":{"statusCheckRollup":{"state":"FAILURE","contexts":{"totalCount":2}}}}]}}
 ]},
 "merged":{"issueCount":12,"nodes":[{"number":477,"title":"NTP","updatedAt":"2026-10-31T00:00:00Z","mergedAt":"2026-10-30T12:00:00Z","repository":{"name":"horodyne"}}]},
 "review":{"issueCount":2},
 "user":{"contributionsCollection":{"contributionCalendar":{"weeks":[
   {"contributionDays":[{"contributionCount":9},{"contributionCount":9}]},
   {"contributionDays":[{"contributionCount":1},{"contributionCount":0},{"contributionCount":0},{"contributionCount":1},{"contributionCount":2},{"contributionCount":0},{"contributionCount":1}]},
   {"contributionDays":[{"contributionCount":0},{"contributionCount":3},{"contributionCount":0},{"contributionCount":1},{"contributionCount":2},{"contributionCount":4},{"contributionCount":5},{"contributionCount":0}]}
  ]}},
  "repositories":{"nodes":[
   {"name":"valorax","defaultBranchRef":{"name":"main","target":{"checkSuites":{"nodes":[{"status":"COMPLETED","conclusion":"SUCCESS","updatedAt":"2026-11-01T06:00:00Z","workflowRun":{"runNumber":7,"workflow":{"name":"Deploy docs"}},"checkRuns":{"nodes":[]}}]}}}},
   {"name":"horodyne","defaultBranchRef":{"name":"main","target":{"checkSuites":{"nodes":[{"status":"IN_PROGRESS","conclusion":null,"updatedAt":"2026-11-01T07:00:00Z","workflowRun":{"runNumber":1042,"workflow":{"name":"Build firmware"}},"checkRuns":{"nodes":[
     {"name":"lint","status":"COMPLETED","conclusion":"SUCCESS","startedAt":"2026-11-01T07:00:00Z","completedAt":"2026-11-01T07:00:21Z"},
     {"name":"build","status":"IN_PROGRESS","conclusion":null,"startedAt":"2026-11-01T07:01:00Z","completedAt":null},
     {"name":"release","status":"QUEUED","conclusion":null},
     {"name":"bad","status":"COMPLETED","conclusion":"FAILURE","startedAt":"2026-11-01T07:00:00Z","completedAt":"2026-11-01T07:00:01Z"}
   ]}}]}}}},
   {"name":"empty","defaultBranchRef":null},
   {"name":"nosuite","defaultBranchRef":{"name":"main","target":{"checkSuites":{"nodes":[]}}}}
  ]}}}})";

static const char COMMITS[] = R"({"items":[
 {"sha":"a3f91c2deadbeef","commit":{"message":"Fix USB offset\n\nLonger body","author":{"date":"2026-11-01T05:00:00Z"}},"repository":{"name":"horodyne"}}
]})";

int main() {
    static GitHub g;
    JsonDocument doc;
    assert(!deserializeJson(doc, REPLY, DeserializationOption::NestingLimit(GH_JSON_DEPTH)));
    assert(ghParseData(doc, g));

    assert(g.openCount == 7 && g.mergedCount == 12 && g.reviewCount == 2);
    assert(g.openShown == 4 && g.mergedShown == 1);
    assert(g.open[0].number == 482 && !strcmp(g.open[0].repo, "horodyne"));
    assert(!strcmp(g.open[0].title, "Fix battery ? drift"));     // font is ASCII only
    assert(g.open[0].ci == GH_PASS && g.open[0].checks == 5);
    assert(g.open[0].added == 128 && g.open[0].removed == 34 && g.open[0].files == 6);
    assert(g.open[0].approvals == 1 && g.open[0].reviewers == 2);
    assert(g.open[1].ci == GH_NONE && g.open[2].ci == GH_RUN && g.open[3].ci == GH_FAIL);
    assert(g.merged[0].when == ghParseTime("2026-10-30T12:00:00Z"));   // merge time, not last update

    // 17 days given: the first three drop off, today stays last.
    assert(g.days[13] == 0 && g.days[12] == 5 && g.days[0] == 0);
    assert(g.weekTotal == 3 + 0 + 1 + 2 + 4 + 5 + 0);
    assert(g.streak == 4);                     // empty today skipped: 5, 4, 2, 1 then a zero

    assert(g.runCount == 2);                   // repos with no suite are skipped
    assert(!strcmp(g.runs[1].name, "Build firmware") && g.runs[1].number == 1042);
    assert(g.runs[1].state == GH_RUN && g.runs[1].jobCount == 4);
    assert(g.runs[1].jobs[0].state == GH_PASS && g.runs[1].jobs[0].secs == 21);
    assert(g.runs[1].jobs[1].state == GH_RUN && g.runs[1].jobs[2].state == GH_NONE && g.runs[1].jobs[3].state == GH_FAIL);
    assert(g.runs[1].jobsDone() == 1);
    assert(ghHeadRun(g) == &g.runs[1]);        // running beats newest-listed

    doc.clear();
    assert(!deserializeJson(doc, COMMITS, DeserializationOption::NestingLimit(GH_JSON_DEPTH)));
    ghParseCommits(doc, g);
    assert(g.commitCount == 1 && !strcmp(g.commits[0].sha, "a3f91c2"));
    assert(!strcmp(g.commits[0].message, "Fix USB offset"));   // first line only

    assert(ghParseTime("2026-11-01T07:30:00Z") == 1793518200);
    assert(ghParseTime("garbage") == 0 && ghParseTime(nullptr) == 0);
    assert(ghValidLogin("senju-dev") && !ghValidLogin("a b") && !ghValidLogin("x\" is:public") && !ghValidLogin("") && !ghValidLogin("-a"));

    char b[8];
    ghAge(b, sizeof(b), 1000, 1000 + 120);    assert(!strcmp(b, "2M"));
    ghAge(b, sizeof(b), 1000, 1000 + 7200);   assert(!strcmp(b, "2H"));
    ghAge(b, sizeof(b), 1000, 1000 + 172800); assert(!strcmp(b, "2D"));
    ghAge(b, sizeof(b), 2000, 1000);          assert(!strcmp(b, "NOW"));   // clock moved back

    // An API error body must not count as data.
    doc.clear();
    deserializeJson(doc, R"({"errors":[{"message":"bad"}]})");
    assert(!ghParseData(doc, g));
    puts("github parser tests passed");
}
