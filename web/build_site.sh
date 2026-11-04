#!/bin/bash
# web/build_site.sh: the web page in web/_site, ready for GitHub Pages.
#   the player as WebAssembly (build.sh), jsbeeb built from its repository at a pinned commit (its own vite build,
#   unchanged), the page's script bundled (esbuild). Elite itself is fetched by the page from elite.bbcelite.com.
set -e
cd "$(dirname "$0")"
JSBEEB_COMMIT=${JSBEEB_COMMIT:-c6ac7200d284376768458a993285192aa4d477f2}
J=${JSBEEB:-.jsbeeb}

if [ ! -d "$J/.git" ]; then git init -q "$J" && git -C "$J" remote add origin https://github.com/mattgodbolt/jsbeeb.git; fi
if [ "$(git -C "$J" rev-parse HEAD 2>/dev/null)" != "$JSBEEB_COMMIT" ]; then
    git -C "$J" fetch -q --depth 1 origin "$JSBEEB_COMMIT" && git -C "$J" checkout -q FETCH_HEAD
fi
[ -d "$J/node_modules" ] || (cd "$J" && npm ci --no-audit --no-fund -s)
(cd "$J" && npx vite build --logLevel error)
./build.sh
[ -d node_modules/esbuild ] || npm ci --no-audit --no-fund -s
rm -rf _site && mkdir -p _site
cp -r "$J/dist" _site/jsbeeb && rm -rf _site/jsbeeb/discs _site/jsbeeb/benchmarks && mkdir -p _site/jsbeeb/discs
# (Elite itself is not part of the site: the page loads it from elite.bbcelite.com. LOCAL_DISC=1, for testing where that
# site can't be reached: the disc from rp2350-bbc-micro/discs, served at ?disc=elite.ssd)
[ -n "$LOCAL_DISC" ] && cp ../rp2350-bbc-micro/discs/elite-master-flicker-free-sng47.ssd _site/jsbeeb/discs/elite.ssd
npx esbuild src/main.js --bundle --format=esm --minify --outfile=_site/app.js --define:JSBEEB_COMMIT="\"$JSBEEB_COMMIT\"" --alias:jsbeeb-src="$(cd "$J" && pwd)/src" \
  --external:fs --external:path --external:url --external:assert --external:util --external:'node:*' --log-level=warning
cp index.html style.css player.wasm panel.json galaxies.json gh.png _site/ && cp -r chapters _site/
# jsbeeb is GPL-3.0-or-later: its licence beside its build, and where its source is (the commit this site was built from).
# The page's own bundle (app.js) includes two of jsbeeb's modules, so the page as a whole is offered under the GPL too
cp "$J/LICENSE" _site/jsbeeb/LICENSE
printf 'jsbeeb (c) Matt Godbolt and contributors, GPL-3.0-or-later (LICENSE).\nSource: https://github.com/mattgodbolt/jsbeeb/tree/%s\n' "$JSBEEB_COMMIT" > _site/jsbeeb/SOURCE.txt
printf 'elite-agent web page. Its source: https://github.com/8bitkick/elite-agent (web/). app.js bundles two GPL-3.0 modules of\njsbeeb (snapshot.js, disc.js; https://github.com/mattgodbolt/jsbeeb/tree/%s), so this page as a whole is offered under the\nGPL-3.0-or-later; elite-agent'"'"'s own code is also MIT licensed. Elite (c) Ian Bell and David Braben is not part of this site:\nthe page loads Mark Moxon'"'"'s flicker-free disc from elite.bbcelite.com.\n' "$JSBEEB_COMMIT" > _site/LICENSES.txt
touch _site/.nojekyll
du -sh _site
