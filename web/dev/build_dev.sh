#!/bin/bash
# web/dev/build_dev.sh [out]: the dev page, in web/dev/_site unless out is given: index.html, app.js and the pinned core.wasm
# (its hash checked; nothing is compiled). It sits
# one level below the main page, whose style.css, panel.json, galaxies.json and images it uses.
set -e
cd "$(dirname "$0")"
OUT=${1:-_site}
sha256sum -c core.wasm.sha256
[ -d ../node_modules/esbuild ] || (cd .. && npm ci --no-audit --no-fund -s)
rm -rf "$OUT" && mkdir -p "$OUT"
AV=$(cat core.wasm | sha1sum | cut -c1-10)
../node_modules/.bin/esbuild main.js --bundle --format=esm --minify --outfile="$OUT/app.js" --define:ASSET_V="\"$AV\"" --log-level=warning
cp core.wasm "$OUT/"
V=$(cat "$OUT/app.js" | sha1sum | cut -c1-10)
sed -e '/googletagmanager/d' -e '/<script>/,/<\/script>/d' -e '/<iframe id="beeb"/d' \
    -e 's|href="style.css"|href="../style.css"|' -e "s|src=\"app.js\"|src=\"app.js?v=$V\"|" -e 's|src="gh.png"|src="../gh.png"|g' \
    -e 's|<title>elite-agent</title>|<title>elite-agent dev</title><meta name="robots" content="noindex">|' ../index.html > "$OUT/index.html"
printf 'The elite-agent dev page, GPL-3.0-or-later. Source: https://github.com/8bitkick/elite-agent (web/dev/). core.wasm includes Kieran Connell'"'"'s\nBBC Master core (https://github.com/kieranhj/esp32-beeb, GPL-3.0). Its ROMs and Elite are fetched by the browser and are not part of this site.\n' > "$OUT/LICENSES.txt"
ls -l "$OUT"
