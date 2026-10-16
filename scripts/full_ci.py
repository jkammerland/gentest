#!/usr/bin/env python3
"""Trusted metadata controller for on-demand PR validation.

This script never checks out or executes candidate code. Workers receive only a
validated PR number and immutable commits through reusable-workflow inputs.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
from urllib.error import HTTPError
from urllib.parse import quote
from urllib.request import Request, urlopen


MARKER = "<!-- gentest-full-ci:v1\n"
CHECKBOX = "Run full CI for this revision"
BOT = "github-actions[bot]"
WRITE_PERMISSIONS = {"write", "maintain", "admin"}
ACTIVE_STATUSES = {"queued", "in_progress", "waiting", "pending", "requested"}
SHA = re.compile(r"[0-9a-f]{40}")


class ApiError(RuntimeError):
    def __init__(self, status: int):
        super().__init__(f"GitHub API returned HTTP {status}")
        self.status = status


class GitHub:
    def __init__(self, repository: str, token: str, api_url: str = "https://api.github.com"):
        if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository):
            raise ValueError("Invalid repository")
        self.repository = repository
        self.token = token
        self.api_url = api_url.rstrip("/")

    def call(self, method: str, path: str, body: dict | None = None):
        payload = json.dumps(body).encode() if body is not None else None
        request = Request(f"{self.api_url}/repos/{self.repository}/{path}", data=payload, method=method, headers={
            "Authorization": f"Bearer {self.token}",
            "Accept": "application/vnd.github+json",
            "X-GitHub-Api-Version": "2022-11-28",
            "Content-Type": "application/json",
        })
        try:
            with urlopen(request, timeout=30) as response:
                return json.load(response)
        except HTTPError as error:
            raise ApiError(error.code) from None

    def comments(self, number: int):
        # Pagination is bounded by the actual API result, not a truncated inventory.
        page = 1
        while True:
            comments = self.call("GET", f"issues/{number}/comments?per_page=100&page={page}")
            yield from comments
            if len(comments) < 100:
                return
            page += 1


def snapshot(pr: dict) -> tuple[str, str]:
    head, base = pr["head"]["sha"], pr["base"]["sha"]
    if not SHA.fullmatch(head) or not SHA.fullmatch(base):
        raise ValueError("Invalid PR commit")
    return head, base


def parse_state(body: str) -> dict | None:
    if not body.startswith(MARKER):
        return None
    try:
        state = json.loads(body[len(MARKER):body.index("\n-->", len(MARKER))])
        if not isinstance(state, dict):
            return None
        if type(state["pr"]) is not int or state["pr"] <= 0:
            return None
        if type(state["comment_id"]) is not int or state["comment_id"] < 0:
            return None
        if type(state["run_id"]) is not int or state["run_id"] < 0:
            return None
        if not SHA.fullmatch(state["head"]) or not SHA.fullmatch(state["base"]):
            return None
        return state
    except (KeyError, ValueError, TypeError):
        return None


def is_bot(comment: dict) -> bool:
    return comment.get("user", {}).get("login") == BOT and comment["user"].get("type") == "Bot"


def find_prompt(api: GitHub, number: int) -> tuple[dict | None, dict | None]:
    for comment in api.comments(number):
        state = parse_state(comment.get("body", ""))
        if is_bot(comment) and state and state["pr"] == number and state["comment_id"] in {0, comment["id"]}:
            return comment, state
    return None, None


def run_url(api: GitHub, run_id: int) -> str:
    return f"https://github.com/{api.repository}/actions/runs/{run_id}"


def render(api: GitHub, state: dict, note: str = "") -> str:
    body = MARKER + json.dumps(state, sort_keys=True, separators=(",", ":")) + "\n-->\n"
    body += "Full CI runs only when a maintainer with write, maintain, or admin access requests it.\n\n"
    body += f"Head: `{state['head']}`\nBase: `{state['base']}`\n\n"
    body += f"- [ ] {CHECKBOX}\n\n"
    body += "Click the checkbox above, or post an exact `/full-ci` comment. The command always remains available.\n\n"
    if state["run_id"]:
        body += f"Full CI: **{state['status']}** — [run {state['run_id']}]({run_url(api, state['run_id'])}).\n"
        if state.get("merge"):
            body += f"Tested merge commit: `{state['merge']}`.\n"
    else:
        body += "Waiting for a maintainer request.\n"
    if note:
        body += f"\n{note}\n"
    return body + "\nResults are informational and do not change merge requirements. A changed head or base needs a fresh request.\n"


def waiting(number: int, pr: dict, comment_id: int = 0) -> dict:
    head, base = snapshot(pr)
    return {"pr": number, "head": head, "base": base, "comment_id": comment_id, "run_id": 0, "status": "waiting"}


def save(api: GitHub, comment: dict, state: dict, note: str = ""):
    return api.call("PATCH", f"issues/comments/{comment['id']}", {"body": render(api, state, note)})


def ensure_prompt(api: GitHub, number: int, pr: dict) -> tuple[dict, dict]:
    comment, state = find_prompt(api, number)
    if comment:
        return comment, state

    # GitHub assigns the ID; a second edit binds the marker to that exact comment.
    state = waiting(number, pr)
    comment = api.call("POST", f"issues/{number}/comments", {"body": render(api, state)})
    state["comment_id"] = comment["id"]
    comment = save(api, comment, state)
    return comment, state


def prompt(api: GitHub, event: dict) -> str:
    if event.get("action") == "edited" and "base" not in event.get("changes", {}):
        return "No base change."
    number = event["pull_request"]["number"]
    pr = api.call("GET", f"pulls/{number}")
    if pr["state"] != "open" or snapshot(pr) != snapshot(event["pull_request"]):
        return "Obsolete PR event ignored."

    comment, state = ensure_prompt(api, number, pr)
    latest = api.call("GET", f"pulls/{number}")
    if latest["state"] != "open" or snapshot(latest) != snapshot(pr):
        return "PR changed before prompt publication."
    if (state["head"], state["base"]) != snapshot(pr):
        save(api, comment, waiting(number, pr, comment["id"]), "The revision changed; request full CI again.")
    return "Full CI prompt is current."


def checked(body: str) -> bool | None:
    matches = re.findall(rf"^- \[([ xX])\] {CHECKBOX}$", body, re.MULTILINE)
    if len(matches) != 1:
        return None
    return matches[0].lower() == "x"


def permission(api: GitHub, sender: dict) -> bool:
    if sender.get("type") != "User" or not sender.get("login"):
        return False
    try:
        result = api.call("GET", f"collaborators/{quote(sender['login'], safe='')}/permission")
    except ApiError as error:
        if error.status in {403, 404}:
            return False
        raise
    return result.get("permission") in WRITE_PERMISSIONS


def request_run(api: GitHub, event: dict, run_id: int) -> dict:
    denied = {"ready": "false"}
    if event.get("action") not in {"created", "edited"} or not event.get("issue", {}).get("pull_request"):
        return {**denied, "message": "Not a PR comment request."}
    comment_event = event["comment"]
    body = comment_event.get("body", "")
    command = body.strip() == "/full-ci"
    before = event.get("changes", {}).get("body", {}).get("from", "")
    checkbox = event["action"] == "edited" and checked(before) is False and checked(body) is True
    if not (command or checkbox) or not permission(api, event.get("sender", {})):
        return {**denied, "message": "Request is absent or sender lacks live repository write access."}

    # Reject queued events for a comment that has since been edited again.
    live_comment = api.call("GET", f"issues/comments/{comment_event['id']}")
    if live_comment.get("body") != body or live_comment.get("updated_at") != comment_event.get("updated_at"):
        return {**denied, "message": "Stale comment event ignored."}
    number = event["issue"]["number"]
    pr = api.call("GET", f"pulls/{number}")
    if pr["state"] != "open" or pr["base"]["repo"]["full_name"] != api.repository:
        return {**denied, "message": "PR is closed or belongs to another repository."}
    comment, state = ensure_prompt(api, number, pr)
    head, base = snapshot(pr)
    if checkbox:
        old_state = parse_state(before)
        if not is_bot(live_comment) or comment["id"] != live_comment["id"] or old_state != state:
            return {**denied, "message": "Checkbox is not the canonical bot comment and unchanged marker."}
        if (state["head"], state["base"]) != (head, base):
            save(api, comment, waiting(number, pr, comment["id"]), "That checkbox referred to an older revision. Request again.")
            return {**denied, "message": "Stale checkbox revision refused."}

    # A duplicate active request links to the existing run instead of replacing it.
    if state["run_id"] and (state["head"], state["base"]) == (head, base):
        old_run = api.call("GET", f"actions/runs/{state['run_id']}")
        if old_run["status"] in ACTIVE_STATUSES:
            save(api, comment, state, "A full-CI run is already active for this revision.")
            return {**denied, "message": f"Already active: {run_url(api, state['run_id'])}"}

    # Never guess while GitHub is calculating mergeability; the command can be retried.
    if pr.get("mergeable") is not True or not SHA.fullmatch(pr.get("merge_commit_sha") or ""):
        save(api, comment, state, "GitHub has not provided a conflict-free merge revision. Resolve conflicts or retry `/full-ci`.")
        return {**denied, "message": "Merge conflict or mergeability is not ready."}
    merge = pr["merge_commit_sha"]
    commit = api.call("GET", f"git/commits/{merge}")
    if [parent["sha"] for parent in commit["parents"]] != [base, head]:
        return {**denied, "message": "Merge commit does not match the current PR head and base."}
    latest = api.call("GET", f"pulls/{number}")
    if latest["state"] != "open" or snapshot(latest) != (head, base) or latest.get("merge_commit_sha") != merge:
        return {**denied, "message": "PR changed while authorizing the request."}

    state = {**waiting(number, pr, comment["id"]), "run_id": run_id, "merge": merge, "status": "running"}
    save(api, comment, state)
    return {"ready": "true", "pr_number": str(number), "head_sha": head, "base_sha": base,
            "candidate_sha": merge, "message": f"Started {run_url(api, run_id)}"}


def report(api: GitHub, number: int, head: str, base: str, merge: str, run_id: int, result: str) -> str:
    if result not in {"success", "failure", "cancelled", "skipped"}:
        result = "failure"
    comment, state = find_prompt(api, number)
    if not comment or state["run_id"] != run_id or (state["head"], state["base"], state.get("merge")) != (head, base, merge):
        return "A newer request owns the prompt; old result ignored."

    # Serialized metadata jobs also recheck live commits immediately before publishing.
    pr = api.call("GET", f"pulls/{number}")
    if snapshot(pr) != (head, base):
        return "Head or base changed; old result ignored."
    save(api, comment, {**state, "status": result})
    return f"Published {result}: {run_url(api, run_id)}"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("prompt", "request", "report"))
    args = parser.parse_args()
    api = GitHub(os.environ["GITHUB_REPOSITORY"], os.environ["GH_TOKEN"], os.environ.get("GITHUB_API_URL", "https://api.github.com"))
    event = json.loads(Path(os.environ["GITHUB_EVENT_PATH"]).read_text(encoding="utf-8"))
    run_id = int(os.environ["GITHUB_RUN_ID"])
    if args.mode == "prompt":
        result = {"message": prompt(api, event)}
    elif args.mode == "request":
        result = request_run(api, event, run_id)
        with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as output:
            for name, value in result.items():
                if name != "message":
                    output.write(f"{name}={value}\n")
    else:
        result = {"message": report(api, int(os.environ["CI_PR_NUMBER"]), os.environ["CI_HEAD_SHA"],
                                    os.environ["CI_BASE_SHA"], os.environ["CI_CANDIDATE_SHA"], run_id,
                                    os.environ["CI_RESULT"])}
    print(result["message"])
    with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as summary:
        summary.write(result["message"] + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
