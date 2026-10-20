# The plugin interface

`app_api.h` and `machine_hook.h` are copies of [rp2350-bbc-micro](https://github.com/8bitkick/rp2350-bbc-micro)'s
`src/common/` headers at commit `b823298` (`APP_API_VERSION` 1): the interface `presto/app/build.sh`
builds the plugin against. Copy them again when the firmware's `APP_API_VERSION` changes.
