#!/usr/bin/env python3
"""Exercise the trusted PR controller against a stateful mocked GitHub API."""

from __future__ import annotations

import copy
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import full_ci

HEAD, BASE, MERGE = "a" * 40, "b" * 40, "c" * 40
NEXT_HEAD, NEXT_BASE, NEXT_MERGE = "d" * 40, "e" * 40, "f" * 40


class FakeGitHub:
    repository = "owner/gentest"

    def __init__(self):
        self.calls = []
        self.prs = {7: self.pr(7)}
        self.stored_comments = {}
        self.runs = {}
        self.parents = {MERGE: [BASE, HEAD], NEXT_MERGE: [NEXT_BASE, NEXT_HEAD]}
        self.granted = "write"
        self.next_id = 100
        self.pr_reads = []

    @staticmethod
    def pr(number):
        return {"number": number, "state": "open", "mergeable": True, "merge_commit_sha": MERGE,
                "head": {"sha": HEAD, "repo": {"full_name": "fork/gentest"}},
                "base": {"sha": BASE, "repo": {"full_name": "owner/gentest"}}}

    def call(self, method, path, body=None):
        self.calls.append((method, path, copy.deepcopy(body)))
        bits = path.split("/")
        if method == "GET" and bits[0] == "collaborators":
            if isinstance(self.granted, int):
                raise full_ci.ApiError(self.granted)
            return {"permission": self.granted}
        if method == "GET" and bits[0] == "pulls":
            return copy.deepcopy(self.pr_reads.pop(0) if self.pr_reads else self.prs[int(bits[1])])
        if method == "GET" and bits[:2] == ["issues", "comments"]:
            return copy.deepcopy(self.stored_comments[int(bits[2])])
        if method == "POST" and bits[0] == "issues":
            self.next_id += 1
            comment = {"id": self.next_id, "body": body["body"], "issue": int(bits[1]),
                       "updated_at": "2026-10-16T10:00:00Z", "user": {"login": full_ci.BOT, "type": "Bot"}}
            self.stored_comments[comment["id"]] = comment
            return copy.deepcopy(comment)
        if method == "PATCH" and bits[:2] == ["issues", "comments"]:
            comment = self.stored_comments[int(bits[2])]
            comment["body"] = body["body"]
            return copy.deepcopy(comment)
        if method == "GET" and bits[:2] == ["actions", "runs"]:
            return copy.deepcopy(self.runs[int(bits[2])])
        if method == "GET" and bits[:2] == ["git", "commits"]:
            return {"parents": [{"sha": sha} for sha in self.parents[bits[2]]]}
        raise AssertionError(f"Unexpected API operation: {method} {path}")

    def comments(self, number):
        return [copy.deepcopy(comment) for comment in self.stored_comments.values() if comment["issue"] == number]

    def prompt(self, number=7):
        return full_ci.ensure_prompt(self, number, self.prs[number])

    def command(self, number=7, action="created", body="/full-ci"):
        self.next_id += 1
        comment = {"id": self.next_id, "body": body, "issue": number, "updated_at": "2026-10-16T10:01:00Z",
                   "user": {"login": "maintainer", "type": "User"}}
        self.stored_comments[comment["id"]] = copy.deepcopy(comment)
        return {"action": action, "issue": {"number": number, "pull_request": {"url": "https://api.github.com/repos/owner/gentest/pulls/7"}}, "comment": comment,
                "sender": {"login": "maintainer", "type": "User"}}

    def checkbox(self, number=7):
        comment, state = self.prompt(number)
        before = comment["body"]
        comment["body"] = before.replace("- [ ] " + full_ci.CHECKBOX, "- [x] " + full_ci.CHECKBOX)
        comment["updated_at"] = "2026-10-16T10:02:00Z"
        self.stored_comments[comment["id"]] = copy.deepcopy(comment)
        return {"action": "edited", "issue": {"number": number, "pull_request": {"url": "https://api.github.com/repos/owner/gentest/pulls/7"}}, "comment": comment,
                "changes": {"body": {"from": before}}, "sender": {"login": "maintainer", "type": "User"}}

    def state(self, number=7):
        return full_ci.find_prompt(self, number)[1]


class RequestTests(unittest.TestCase):
    def setUp(self):
        self.api = FakeGitHub()

    def test_fork_command_freezes_current_merge_base_and_head(self):
        result = full_ci.request_run(self.api, self.api.command(), 900)
        self.assertEqual(result, {"ready": "true", "pr_number": "7", "head_sha": HEAD, "base_sha": BASE,
                                  "candidate_sha": MERGE, "message": "Started https://github.com/owner/gentest/actions/runs/900"})
        self.assertEqual(self.api.state()["merge"], MERGE)
        self.assertIn(("GET", "collaborators/maintainer/permission", None), self.api.calls)

    def test_author_association_never_grants_permission(self):
        for access in ("read", "triage", "none", 403, 404):
            with self.subTest(access=access):
                self.api = FakeGitHub()
                self.api.granted = access
                event = self.api.command()
                event["comment"]["author_association"] = "OWNER"
                self.assertEqual(full_ci.request_run(self.api, event, 900)["ready"], "false")
                self.assertFalse(any(method in {"POST", "PATCH"} for method, _, _ in self.api.calls))
        for access in ("write", "maintain", "admin"):
            with self.subTest(access=access):
                self.api = FakeGitHub()
                self.api.granted = access
                self.assertEqual(full_ci.request_run(self.api, self.api.command(), 900)["ready"], "true")

    def test_exact_command_only_and_bots_do_not_loop(self):
        for body in ("/full-ci please", "/full-ci\nextra", "`/full-ci`", "quoted /full-ci", ""):
            with self.subTest(body=body):
                self.assertEqual(full_ci.request_run(self.api, self.api.command(body=body), 900)["ready"], "false")
        event = self.api.command()
        event["sender"] = {"login": full_ci.BOT, "type": "Bot"}
        self.assertEqual(full_ci.request_run(self.api, event, 900)["ready"], "false")
        event = self.api.command()
        event["issue"].pop("pull_request")
        self.assertEqual(full_ci.request_run(self.api, event, 900)["ready"], "false")

    def test_edited_command_uses_live_snapshot_and_rejects_stale_comment(self):
        event = self.api.command(action="edited")
        self.api.prs[7]["head"]["sha"] = NEXT_HEAD
        self.api.prs[7]["base"]["sha"] = NEXT_BASE
        self.api.prs[7]["merge_commit_sha"] = NEXT_MERGE
        result = full_ci.request_run(self.api, event, 900)
        self.assertEqual((result["head_sha"], result["base_sha"], result["candidate_sha"]), (NEXT_HEAD, NEXT_BASE, NEXT_MERGE))
        event = self.api.command()
        self.api.stored_comments[event["comment"]["id"]]["updated_at"] = "later"
        self.assertIn("Stale", full_ci.request_run(self.api, event, 901)["message"])

    def test_checkbox_requires_bot_id_marker_and_unchecked_transition(self):
        event = self.api.checkbox()
        self.assertEqual(full_ci.request_run(self.api, event, 900)["ready"], "true")
        self.assertFalse(full_ci.checked(self.api.stored_comments[event["comment"]["id"]]["body"]))
        for mutation in ("human", "wrong-id", "marker", "checked-before", "created"):
            with self.subTest(mutation=mutation):
                api = FakeGitHub()
                event = api.checkbox()
                if mutation == "human":
                    event["comment"]["user"] = {"login": "maintainer", "type": "User"}
                elif mutation == "wrong-id":
                    event["comment"]["id"] += 50
                elif mutation == "marker":
                    event["changes"]["body"]["from"] = event["changes"]["body"]["from"].replace(HEAD, NEXT_HEAD)
                elif mutation == "checked-before":
                    event["changes"]["body"]["from"] = event["comment"]["body"]
                else:
                    event["action"] = "created"
                api.stored_comments[event["comment"]["id"]] = copy.deepcopy(event["comment"])
                self.assertEqual(full_ci.request_run(api, event, 900)["ready"], "false")

    def test_stale_checkbox_and_changed_during_request_do_not_run(self):
        event = self.api.checkbox()
        self.api.prs[7]["head"]["sha"] = NEXT_HEAD
        self.assertEqual(full_ci.request_run(self.api, event, 900)["ready"], "false")
        self.assertEqual(self.api.state()["head"], NEXT_HEAD)
        self.assertEqual(self.api.state()["run_id"], 0)
        api = FakeGitHub()
        changed = copy.deepcopy(api.prs[7])
        changed["base"]["sha"] = NEXT_BASE
        api.pr_reads = [api.prs[7], changed]
        self.assertIn("changed", full_ci.request_run(api, api.command(), 900)["message"])
        self.assertEqual(api.state()["run_id"], 0)

    def test_conflict_unknown_mergeability_and_wrong_parents_refuse(self):
        for mergeable in (False, None):
            with self.subTest(mergeable=mergeable):
                api = FakeGitHub()
                api.prs[7]["mergeable"] = mergeable
                self.assertEqual(full_ci.request_run(api, api.command(), 900)["ready"], "false")
        self.api.parents[MERGE] = [NEXT_BASE, HEAD]
        self.assertEqual(full_ci.request_run(self.api, self.api.command(), 900)["ready"], "false")
        self.api.prs[7]["state"] = "closed"
        self.assertEqual(full_ci.request_run(self.api, self.api.command(), 900)["ready"], "false")

    def test_duplicate_active_returns_existing_link_and_cancelled_can_restart(self):
        full_ci.request_run(self.api, self.api.command(), 900)
        for status in full_ci.ACTIVE_STATUSES:
            with self.subTest(status=status):
                self.api.runs[900] = {"status": status}
                result = full_ci.request_run(self.api, self.api.command(), 901)
                self.assertEqual(result["ready"], "false")
                self.assertIn("/runs/900", result["message"])
                self.assertEqual(self.api.state()["run_id"], 900)
        self.api.runs[900] = {"status": "completed", "conclusion": "cancelled"}
        self.assertEqual(full_ci.request_run(self.api, self.api.command(), 902)["ready"], "true")
        self.assertEqual(self.api.state()["run_id"], 902)

    def test_new_revision_supersedes_old_request_and_parallel_prs_stay_independent(self):
        full_ci.request_run(self.api, self.api.command(), 900)
        self.api.prs[7]["head"]["sha"] = NEXT_HEAD
        self.api.prs[7]["base"]["sha"] = NEXT_BASE
        self.api.prs[7]["merge_commit_sha"] = NEXT_MERGE
        self.assertEqual(full_ci.request_run(self.api, self.api.command(), 901)["ready"], "true")
        self.api.prs[8] = self.api.pr(8)
        self.assertEqual(full_ci.request_run(self.api, self.api.command(8), 902)["pr_number"], "8")
        self.assertEqual(self.api.state(7)["run_id"], 901)
        self.assertEqual(self.api.state(8)["run_id"], 902)
        self.assertNotEqual(self.api.state(7)["comment_id"], self.api.state(8)["comment_id"])

    def test_cli_writes_only_immutable_worker_inputs(self):
        event = self.api.command()
        with tempfile.TemporaryDirectory() as tmp:
            event_file, output, summary = (Path(tmp) / name for name in ("event.json", "output", "summary"))
            event_file.write_text(json.dumps(event))
            env = {"GITHUB_REPOSITORY": self.api.repository, "GH_TOKEN": "test-token", "GITHUB_RUN_ID": "900",
                   "GITHUB_EVENT_PATH": str(event_file), "GITHUB_OUTPUT": str(output), "GITHUB_STEP_SUMMARY": str(summary),
                   "GITHUB_SHA": "0" * 40}
            with patch.dict(os.environ, env), patch.object(sys, "argv", ["full_ci.py", "request"]), \
                    patch.object(full_ci, "GitHub", return_value=self.api):
                self.assertEqual(full_ci.main(), 0)
            self.assertEqual(dict(line.split("=", 1) for line in output.read_text().splitlines()),
                             {"ready": "true", "pr_number": "7", "head_sha": HEAD, "base_sha": BASE, "candidate_sha": MERGE})
            self.assertIn("/runs/900", summary.read_text())


class PromptAndReportTests(unittest.TestCase):
    def setUp(self):
        self.api = FakeGitHub()

    def event(self):
        return {"action": "synchronize", "pull_request": copy.deepcopy(self.api.prs[7])}

    def test_prompt_reuses_one_bot_comment_on_all_current_events(self):
        for action in ("opened", "synchronize", "reopened", "ready_for_review", "edited"):
            event = self.event()
            event.update(action=action, changes={"base": {"ref": {"from": "old"}}})
            full_ci.prompt(self.api, event)
        self.assertEqual(len(self.api.stored_comments), 1)
        state = self.api.state()
        self.assertEqual(state["comment_id"], next(iter(self.api.stored_comments)))
        self.assertIn("/full-ci", next(iter(self.api.stored_comments.values()))["body"])

    def test_stale_prompt_cannot_reset_newer_state(self):
        self.api.prompt()
        event = self.event()
        self.api.prs[7]["head"]["sha"] = NEXT_HEAD
        full_ci.prompt(self.api, event)
        self.assertEqual(self.api.state()["head"], HEAD)
        full_ci.prompt(self.api, self.event())
        self.assertEqual(self.api.state()["head"], NEXT_HEAD)
        event = self.event()
        event["action"] = "edited"
        self.assertEqual(full_ci.prompt(self.api, event), "No base change.")

    def test_completion_updates_only_matching_pair_and_run(self):
        full_ci.request_run(self.api, self.api.command(), 900)
        for result in ("success", "failure", "cancelled", "skipped"):
            with self.subTest(result=result):
                full_ci.report(self.api, 7, HEAD, BASE, MERGE, 900, result)
                self.assertEqual(self.api.state()["status"], result)
        full_ci.report(self.api, 7, HEAD, BASE, MERGE, 899, "success")
        self.assertEqual(self.api.state()["status"], "skipped")
        self.api.prs[7]["base"]["sha"] = NEXT_BASE
        self.assertIn("changed", full_ci.report(self.api, 7, HEAD, BASE, MERGE, 900, "success"))
        self.assertEqual(self.api.state()["status"], "skipped")

    def test_old_result_never_overwrites_a_new_request(self):
        full_ci.request_run(self.api, self.api.command(), 900)
        self.api.runs[900] = {"status": "completed"}
        full_ci.request_run(self.api, self.api.command(), 901)
        self.assertIn("newer", full_ci.report(self.api, 7, HEAD, BASE, MERGE, 900, "success"))
        self.assertEqual(self.api.state()["run_id"], 901)
        self.assertEqual(self.api.state()["status"], "running")

    def test_pagination_does_not_silently_miss_prompt(self):
        with patch.object(full_ci.GitHub, "call", side_effect=[[{"id": i} for i in range(100)], [{"id": 101}]]) as call:
            api = full_ci.GitHub("owner/gentest", "token")
            self.assertEqual(len(list(api.comments(7))), 101)
            self.assertEqual(call.call_args.args, ("GET", "issues/7/comments?per_page=100&page=2"))


if __name__ == "__main__":
    unittest.main()
