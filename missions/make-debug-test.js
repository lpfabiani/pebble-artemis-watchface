#!/usr/bin/env node
/*
 * Builds a debug/test missions/active.json (one event, firing soon, shown
 * for a long time) and commits it to a throwaway git branch — WITHOUT ever
 * touching the working tree or the real `main` branch / active.json.
 *
 * Why a branch instead of editing missions/active.json directly: that file
 * on `main` is exactly what MISSION_URL points every installed watchface
 * at (raw.githubusercontent.com/.../main/missions/active.json). Editing and
 * pushing it would go live to real users immediately. This script instead
 * builds a new commit on a separate branch via git plumbing (hash-object /
 * read-tree / commit-tree), so `git status` stays untouched and nothing
 * lands on `main` until you decide to push.
 *
 * Usage:
 *   node missions/make-debug-test.js [options]
 *
 * Options:
 *   --base=main                 branch/ref to build from (default: main)
 *   --branch=test-mission-data  branch to create/update (default shown)
 *   --in=missions/active.json   source file to base the debug copy on
 *   --minutes-until=2           minutes from now until the event fires
 *   --duration=1440             minutes the banner stays up (1440 = 24h)
 *   --message="DEBUG\nTEST"     banner text (\n splits the two lines)
 *
 * After running, push the branch yourself (no credentials are stored in
 * this environment):
 *   git push origin <branch>
 *
 * Then point MISSION_URL (src/pkjs/index.js) at:
 *   https://raw.githubusercontent.com/<owner>/<repo>/<branch>/missions/active.json
 * temporarily, rebuild/reload the JS, and confirm the debug banner fires.
 * Revert MISSION_URL to `main` before shipping, and delete the test branch:
 *   git push origin --delete <branch> && git branch -D <branch>
 */

const { execFileSync } = require('child_process');
const path = require('path');

function git(args, opts = {}) {
  return execFileSync('git', args, { encoding: 'utf8', ...opts });
}

function parseArgs(argv) {
  const opts = {
    base: 'main',
    branch: 'test-mission-data',
    in: 'missions/active.json',
    minutesUntil: 2,
    duration: 1440,
    message: 'DEBUG\nTEST',
  };
  for (const arg of argv) {
    const m = arg.match(/^--([^=]+)=(.*)$/);
    if (!m) continue;
    const [, key, value] = m;
    switch (key) {
      case 'base': opts.base = value; break;
      case 'branch': opts.branch = value; break;
      case 'in': opts.in = value; break;
      case 'minutes-until': opts.minutesUntil = Number(value); break;
      case 'duration': opts.duration = Number(value); break;
      case 'message': opts.message = value.replace(/\\n/g, '\n'); break;
      default: throw new Error(`Unknown option --${key}`);
    }
  }
  return opts;
}

function main() {
  const opts = parseArgs(process.argv.slice(2));

  // Read the source file from the committed base ref, not the working tree —
  // keeps this independent of whatever is currently checked out / dirty.
  const raw = git(['show', `${opts.base}:${opts.in}`]);
  const mission = JSON.parse(raw);

  const nowEpoch = Math.floor(Date.now() / 1000);
  const debugEvent = {
    epoch: nowEpoch + Math.round(opts.minutesUntil * 60),
    message: opts.message,
    displayMinutes: opts.duration,
  };

  mission.events = mission.events || [];
  mission.events.unshift(debugEvent);

  const newContent = JSON.stringify(mission, null, 2) + '\n';

  // --- Build the new commit via git plumbing, no checkout involved ---
  const baseCommit = git(['rev-parse', opts.base]).trim();
  const baseTree = git(['rev-parse', `${opts.base}^{tree}`]).trim();

  const blobSha = git(['hash-object', '-w', '--stdin'], { input: newContent }).trim();

  // Use a scratch index file so we never touch .git/index.
  const scratchIndex = path.join(git(['rev-parse', '--git-dir']).trim(), 'DEBUG_MISSION_INDEX');
  const env = { ...process.env, GIT_INDEX_FILE: scratchIndex };
  execFileSync('git', ['read-tree', baseTree], { env });
  execFileSync('git', ['update-index', '--add', '--cacheinfo', `100644,${blobSha},${opts.in}`], { env });
  const newTree = execFileSync('git', ['write-tree'], { env, encoding: 'utf8' }).trim();
  execFileSync('git', ['update-index', '--remove'], { env }); // scratch index no longer needed

  const commitMsg = `Debug test mission event (fires in ${opts.minutesUntil}m, ` +
    `shown ${opts.duration}m) — not for main`;
  const newCommit = git(['commit-tree', newTree, '-p', baseCommit, '-m', commitMsg]).trim();

  git(['update-ref', `refs/heads/${opts.branch}`, newCommit]);

  const remoteUrl = git(['remote', 'get-url', 'origin']).trim();
  const match = remoteUrl.match(/[:/]([^/]+)\/([^/.]+?)(\.git)?$/);
  const owner = match ? match[1] : '<owner>';
  const repo = match ? match[2] : '<repo>';
  const rawUrl = `https://raw.githubusercontent.com/${owner}/${repo}/${opts.branch}/${opts.in}`;

  console.log(`Created local branch '${opts.branch}' (commit ${newCommit.slice(0, 12)}), working tree untouched.`);
  console.log(`Debug event fires at epoch ${debugEvent.epoch} (in ~${opts.minutesUntil} min), shown for ${opts.duration} min.`);
  console.log('');
  console.log('Next steps:');
  console.log(`  1. Push it:      git push origin ${opts.branch}`);
  console.log(`  2. Test URL:     ${rawUrl}`);
  console.log(`     Temporarily set MISSION_URL to this in src/pkjs/index.js, reload the app, confirm the banner.`);
  console.log(`  3. Revert MISSION_URL to point at 'main' before shipping.`);
  console.log(`  4. Clean up:     git push origin --delete ${opts.branch} && git branch -D ${opts.branch}`);
}

main();
