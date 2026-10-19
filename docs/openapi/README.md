# NetEase backend contract

Specification: [`openapi.yaml`](openapi.yaml) (OpenAPI 3.1).

This is a **contract document**, not a second client stack. Yunting is a Qt/C++
application; the typed implementation lives in `src/sdk/`. The contract exists
so a reviewer can see request/response shapes, auth, transport and error behavior
in one place, and so future SDK modules have a checked target.

## Structure

```text
docs/openapi/
├── openapi.yaml                 # root: servers, security, tags, path map
├── paths/
│   ├── listen-together.yaml
│   ├── messaging.yaml
│   ├── social.yaml
│   ├── comments.yaml
│   ├── playlists.yaml
│   ├── cloud.yaml
│   ├── account.yaml
│   └── search-lyrics.yaml
└── components/
    ├── responses.yaml           # common 301/400/405/488 envelopes
    └── schemas/
        ├── common.yaml
        ├── listen-together.yaml
        ├── messaging.yaml
        ├── social.yaml
        ├── comments.yaml
        ├── playlists.yaml
        ├── cloud.yaml
        ├── account.yaml
        └── search-lyrics.yaml
```

Every operation has an `operationId`, a tag, an explicit `security` block, an
`x-transport` value (`api` or `weapi`), and at least one documented 4xx response.

Endpoints whose parameters are still unverified stay out of this contract and
are tracked in [`../API-REMAINING.md`](../API-REMAINING.md) §6.

## C++ SDK mapping

| Contract area | C++ implementation |
|---|---|
| Listen Together room/sync | `src/sdk/listen_together.h/.cpp`, exposed through `src/listentogether.h` |
| Private messages | `src/store_inbox.cpp` (read/send) and `src/store.h` slots |
| Friend presence | `src/sdk/friend_listening.h/.cpp` |
| Comments | `src/sdk/comment.h/.cpp`, `src/store_comment*.cpp` |
| Playlists | `src/store_playlist.cpp` |
| Cloud | `src/store_resource.cpp`, `src/downloadmanager.*` |
| Account/VIP | `src/store_account.cpp`, `src/sdk/user_vip.*` |
| Search/lyrics | `src/sdk/search_*.{h,cpp}`, `src/sdk/lyrics.*` |

The Listen Together additions (invite message, per-track privileges, pair
statistics, VIP gift, queue-change notice) are implemented in
`src/sdk/listen_together.*` and covered by `src/tests/listen_together_test.cpp`.

## Validate

```bash
npx --yes @redocly/cli lint docs/openapi/openapi.yaml
```

The document is split with relative `$ref`s, so lint the root, not an individual
path file.

## Rules

- The contract follows the Web-cookie boundary: `MUSIC_U` + `__csrf`, `os=pc`.
  Do not add `/eapi` or `/xeapi` operations.
- A `200` response is only success when `code == 200`. Do not treat HTTP 200
  with a non-200 business code as success.
- `x-transport: weapi` means the C++ transport encrypts the logical JSON body
  with the existing `ncm::weapiForm`; `api` means a plain form/GET.
- Fields marked free-form (`additionalProperties: true`) are intentionally not
  pinned until a live response is captured. Tighten them with a sample, not a
  guess.
