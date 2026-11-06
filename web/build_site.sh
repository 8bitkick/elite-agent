#!/bin/bash
# web/build_site.sh: the web page in web/_site, ready for GitHub Pages: the pinned core.wasm (its hash checked; nothing is
# compiled, see build.sh) and the page's script bundled (esbuild). The ROMs and Elite are fetched by the page, not part of the site.
set -e
cd "$(dirname "$0")"
sha256sum -c core.wasm.sha256
[ -d node_modules/esbuild ] || npm ci --no-audit --no-fund -s
rm -rf _site && mkdir -p _site
AV=$(cat core.wasm panel.json galaxies.json | sha1sum | cut -c1-10)
npx esbuild src/main.js --bundle --format=esm --minify --outfile=_site/app.js --define:ASSET_V="\"$AV\"" --log-level=warning
npx esbuild src/paper.js --bundle --format=esm --minify --outfile=_site/paper.js --log-level=warning
cp index.html style.css core.wasm panel.json galaxies.json gh.png og.png paper.html paper-cmdr.png paper_senses.json _site/
mkdir -p _site/chapters && cp chapters/deadly.cmdr _site/chapters/
V=$(cat _site/app.js _site/style.css | sha1sum | cut -c1-10)
sed -i -e '/<iframe id="beeb"/d' -e "s|href=\"style.css\"|href=\"style.css?v=$V\"|" -e "s|src=\"app.js\"|src=\"app.js?v=$V\"|" _site/index.html
printf 'elite-agent web page. Its source: https://github.com/8bitkick/elite-agent (web/). core.wasm includes Kieran Connell'"'"'s BBC Master core\n(https://github.com/kieranhj/esp32-beeb, GPL-3.0), so this page as a whole is offered under the GPL-3.0-or-later; elite-agent'"'"'s own code is also\nMIT licensed. Elite (c) Ian Bell and David Braben and the Master'"'"'s ROMs are not part of this site: the browser fetches them.\n' > _site/LICENSES.txt
touch _site/.nojekyll
du -sh _site
