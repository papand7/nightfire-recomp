// The games' source the launcher carries: git bundles of this repository's HEAD and of its runtime (sdk) into
// out/source-bundles, and of Agent Under Fire's (AUF_REPO, its repository's folder) into out/source-bundles-auf,
// shipped as the launcher's resources (package.json) so an install needs no download of them. Each game's service is
// given its own folder (main.js): both repositories are named nightfire-recomp.
const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '../..');
const bundles = (repo, out) => {
    fs.mkdirSync(out, { recursive: true });
    const bundle = (dir, name) => {
        const file = path.join(out, name + '.bundle');
        if (fs.existsSync(file)) fs.unlinkSync(file);
        execFileSync('git', ['-C', dir, 'bundle', 'create', file, 'HEAD'], { stdio: 'inherit' });
        console.log(path.basename(out) + '/' + name + '.bundle', Math.round(fs.statSync(file).size / 1048576 * 10) / 10, 'MB');
    };
    // the runtime must be the submodule checked out at the commit the repository names (not the parent repository)
    const sdk = path.join(repo, 'sdk');
    const named = execFileSync('git', ['-C', repo, 'ls-tree', 'HEAD', 'sdk']).toString().split(/\s+/)[2];
    const top = execFileSync('git', ['-C', sdk, 'rev-parse', '--show-toplevel']).toString().trim();
    const head = execFileSync('git', ['-C', sdk, 'rev-parse', 'HEAD']).toString().trim();
    if (path.resolve(top) !== path.resolve(sdk) || head !== named) {
        console.error(`${sdk} is not checked out at ${named} (git submodule update --init sdk)`);
        process.exit(1);
    }
    bundle(repo, 'nightfire-recomp');
    bundle(sdk, 'XboxXBERecomp');
};
bundles(root, path.join(root, 'out', 'source-bundles'));
// (the folder is made even without AUF_REPO: package.json ships it either way; empty, the service downloads the source)
const auf = path.join(root, 'out', 'source-bundles-auf');
fs.rmSync(auf, { recursive: true, force: true });
fs.mkdirSync(auf, { recursive: true });
if (process.env.AUF_REPO) bundles(path.resolve(process.env.AUF_REPO), auf);
else console.log('AUF_REPO is not set: no Agent Under Fire source bundles (its service downloads the source)');
