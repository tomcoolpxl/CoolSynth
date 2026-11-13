"""Download the SFZ mapping files the libraries' author publishes (text only, CC0 like the samples).

They live on a branch of each repository: github.com/sgossner/VCSL "sfz" and
github.com/sgossner/VSCO-2-CE "SFZ". The library builder reads them from <out>/vcsl and <out>/vsco.

usage: python fetch_sfz.py <out-dir>
"""
import json
import os
import sys
import urllib.parse
import urllib.request
from concurrent.futures import ThreadPoolExecutor

out = sys.argv[1]
HEADERS = {"User-Agent": "sfz-fetch"}


def get(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=HEADERS), timeout=60) as response:
        return response.read()


jobs = []
for repo, branch, folder in [("sgossner/VCSL", "sfz", "vcsl"), ("sgossner/VSCO-2-CE", "SFZ", "vsco")]:
    tree = json.loads(get(f"https://api.github.com/repos/{repo}/git/trees/{branch}?recursive=1"))
    for item in tree.get("tree", []):
        if item["path"].lower().endswith(".sfz"):
            jobs.append((f"https://raw.githubusercontent.com/{repo}/{branch}/" + urllib.parse.quote(item["path"]),
                         os.path.join(out, folder, *item["path"].split("/"))))


def fetch(job):
    url, destination = job
    if os.path.exists(destination) and os.path.getsize(destination) > 0:
        return True
    os.makedirs(os.path.dirname(destination), exist_ok=True)
    try:
        data = get(url)
    except Exception as error:  # noqa: BLE001
        print("failed", url, error)
        return False
    with open(destination, "wb") as handle:
        handle.write(data)
    return True


with ThreadPoolExecutor(max_workers=8) as pool:
    results = list(pool.map(fetch, jobs))
print(f"{sum(results)} of {len(jobs)} mapping files present in {out}")
