// The game's source the launcher carries: git bundles of this repository's HEAD and of its runtime (sdk) into
// out/source-bundles, shipped as the launcher's resources (package.json) so an install needs no download of them
const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '../..');
const out = path.join(root, 'out', 'source-bundles');
fs.mkdirSync(out, { recursive: true });
const bundle = (dir, name) => {
    const file = path.join(out, name + '.bundle');
    if (fs.existsSync(file)) fs.unlinkSync(file);
    execFileSync('git', ['-C', dir, 'bundle', 'create', file, 'HEAD'], { stdio: 'inherit' });
    console.log(name + '.bundle', Math.round(fs.statSync(file).size / 1048576 * 10) / 10, 'MB');
};
// the runtime must be the submodule checked out at the commit this repository names (not the parent repository)
const sdk = path.join(root, 'sdk');
const named = execFileSync('git', ['-C', root, 'ls-tree', 'HEAD', 'sdk']).toString().split(/\s+/)[2];
const top = execFileSync('git', ['-C', sdk, 'rev-parse', '--show-toplevel']).toString().trim();
const head = execFileSync('git', ['-C', sdk, 'rev-parse', 'HEAD']).toString().trim();
if (path.resolve(top) !== path.resolve(sdk) || head !== named) {
    console.error(`sdk is not checked out at ${named} (git submodule update --init sdk)`);
    process.exit(1);
}
bundle(root, 'nightfire-recomp');
bundle(sdk, 'XboxXBERecomp');
