// mini-pinyin — lightweight C library for Chinese character → pinyin first letter
//
// Public API.  Implementation uses an embedded sorted array with binary search.
// Data covers ~7000 common CJK Unified Ideographs.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/// Opaque handle to the pinyin dictionary.
typedef struct PinyinDict PinyinDict;

/// Create and initialise a new dictionary instance.
/// Caller must call pinyin_dict_free() when done.
PinyinDict* pinyin_dict_new(void);

/// Destroy a dictionary and free all memory.
void pinyin_dict_free(PinyinDict* dict);

/// Look up the first pinyin letter(s) for a Unicode codepoint.
///
/// @param dict   dictionary handle
/// @param cp     Unicode codepoint (e.g. 0x4E2D for '中')
/// @param buf    output buffer (caller-provided)
/// @param bufsz  size of buf (must be ≥ 8)
/// @return       number of bytes written to buf (excluding NUL),
///               or 0 if the character is not found / not a Chinese character.
///
/// For most characters the return is a single lowercase letter a-z.
/// For multi-pronunciation characters the return is comma-separated
/// letters (e.g. "c,z" for 长).
int pinyin_first_letter(PinyinDict* dict, unsigned int cp,
                        char* buf, int bufsz);

#ifdef __cplusplus
}
#endif
