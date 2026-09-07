# nlohmann/json: header-only, MIT.
#
# Not Qt's QJsonDocument, deliberately. Serialisation lives below the UI so a headless
# exporter can read a project without a GUI toolkit, and so the round-trip is testable
# without booting Qt. Pinned to a release tag.
include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)
