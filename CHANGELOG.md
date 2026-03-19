# Changelog

## 2026-03-19

- integrated `sp.h` implementation via `src/sp_impl.c`
- updated build flags with `-DSP_PS_DISABLE` for Termux compatibility
- migrated `sp_compat` memory/string helpers to `sp_*` APIs
- hardened string builder capacity growth checks against overflow
- rewrote CLI query splitting with `sp_str_split_c8` + `sp_dyn_array`
- fixed CLI parse-error path to release allocated argument buffers
