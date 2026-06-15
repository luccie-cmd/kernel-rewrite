#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define REPORTED_BIT 31
#define CUT_HERE "------------[ cut here ]------------\n"

#if (BITS_PER_LONG == 64) && defined(__BIG_ENDIAN)
#define COLUMN_MASK (~(1U << REPORTED_BIT))
#define LINE_MASK (~0U)
#else
#define COLUMN_MASK (~0U)
#define LINE_MASK (~(1U << REPORTED_BIT))
#endif

#define decltype __typeof__

#define IS_ALIGNED(x, a) (((x) & ((decltype(x))(a) - 1)) == 0)
#define VALUE_LENGTH 40
#define BIT(nr) ((1UL) << (nr))
#if defined(__clang__)
#define UBSAN_NOSAN __attribute__((no_sanitize_address, no_sanitize("undefined")))
#elif defined(__GNUC__)
#define UBSAN_NOSAN __attribute__((no_sanitize_address, no_sanitize_undefined))
#else
#define UBSAN_NOSAN
#endif

static uint64_t        in_ubsan;
static int UBSAN_NOSAN scnprintf(char* buf, size_t size, const char* fmt, ...) {
    int     written;
    va_list args;
    va_start(args, fmt);

    if (size == 0) {
        char dummy;
        written = vsnprintf(&dummy, 0, fmt, args);
    } else {
        written = vsnprintf(buf, size, fmt, args);
        if (written >= (int)size) written = size - 1;
    }
    va_end(args);
    return written;
}

static int UBSAN_NOSAN __ffs(unsigned long word) {
    int bit = 0;
    while (!(word & 1)) {
        word >>= 1;
        bit++;
    }
    return bit;
}

enum ubsan_checks {
    ubsan_add_overflow,
    ubsan_builtin_unreachable,
    ubsan_cfi_check_fail,
    ubsan_divrem_overflow,
    ubsan_dynamic_type_cache_miss,
    ubsan_float_cast_overflow,
    ubsan_function_type_mismatch,
    ubsan_implicit_conversion,
    ubsan_invalid_builtin,
    ubsan_invalid_objc_cast,
    ubsan_load_invalid_value,
    ubsan_missing_return,
    ubsan_mul_overflow,
    ubsan_negate_overflow,
    ubsan_nullability_arg,
    ubsan_nullability_return,
    ubsan_nonnull_arg,
    ubsan_nonnull_return,
    ubsan_out_of_bounds,
    ubsan_pointer_overflow,
    ubsan_shift_out_of_bounds,
    ubsan_sub_overflow,
    ubsan_type_mismatch,
    ubsan_alignment_assumption,
    ubsan_vla_bound_not_positive,
};

enum { type_kind_int = 0, type_kind_float = 1, type_unknown = 0xffff };

typedef struct type_descriptor {
    uint16_t type_kind;
    uint16_t type_info;
    char     type_name[];
} type_descriptor;

typedef struct source_location {
    const char* file_name;
    union {
        unsigned long reported;
        struct {
            uint32_t line;
            uint32_t column;
        };
    };
} source_location;

typedef struct overflow_data {
    struct source_location  location;
    struct type_descriptor* type;
} overflow_data;

typedef struct implicit_conversion_data {
    struct source_location  location;
    struct type_descriptor* from_type;
    struct type_descriptor* to_type;
    unsigned char           type_check_kind;
} implicit_conversion_data;

typedef struct type_mismatch_data {
    struct source_location  location;
    struct type_descriptor* type;
    unsigned long           alignment;
    unsigned char           type_check_kind;
} type_mismatch_data;

typedef struct type_mismatch_data_v1 {
    struct source_location  location;
    struct type_descriptor* type;
    unsigned char           log_alignment;
    unsigned char           type_check_kind;
} type_mismatch_data_v1;

typedef struct type_mismatch_data_common {
    struct source_location* location;
    struct type_descriptor* type;
    unsigned long           alignment;
    unsigned char           type_check_kind;
} type_mismatch_data_common;

typedef struct nonnull_arg_data {
    struct source_location location;
    struct source_location attr_location;
    int                    arg_index;
} nonnull_arg_data;

typedef struct out_of_bounds_data {
    struct source_location  location;
    struct type_descriptor* array_type;
    struct type_descriptor* index_type;
} out_of_bounds_data;

typedef struct shift_out_of_bounds_data {
    struct source_location  location;
    struct type_descriptor* lhs_type;
    struct type_descriptor* rhs_type;
} shift_out_of_bounds_data;

typedef struct unreachable_data {
    struct source_location location;
} unreachable_data;

typedef struct invalid_value_data {
    struct source_location  location;
    struct type_descriptor* type;
} invalid_value_data;

typedef struct alignment_assumption_data {
    struct source_location  location;
    struct source_location  assumption_location;
    struct type_descriptor* type;
} alignment_assumption_data;

typedef struct nonnull_return_data_v1 {
    struct source_location location;
} nonnull_return_data_v1;

typedef struct function_type_mismatch_data {
    struct source_location location;
    struct type_descriptor type;
} function_type_mismatch_data;

static inline bool UBSAN_NOSAN test_and_set_bit(int bit, uint64_t* addr) {
    uint32_t mask    = 1U << (bit % 32);
    bool     was_set = (*addr & mask) != 0;
    *addr |= mask;
    return was_set;
}

static const char* const type_check_kinds[] = {"load of",
                                               "store to",
                                               "reference binding to",
                                               "member access within",
                                               "member call on",
                                               "constructor call on",
                                               "downcast of",
                                               "downcast of"};

static bool UBSAN_NOSAN was_reported(struct source_location* location) {
    return test_and_set_bit(REPORTED_BIT, &location->reported);
}

static bool UBSAN_NOSAN suppress_report(struct source_location* loc) {
    return in_ubsan || was_reported(loc);
}

static bool UBSAN_NOSAN type_is_int(struct type_descriptor* type) {
    return type->type_kind == type_kind_int;
}

static bool UBSAN_NOSAN type_is_signed(struct type_descriptor* type) {
    return type->type_info & 1;
}

static unsigned UBSAN_NOSAN type_bit_width(struct type_descriptor* type) {
    return 1 << (type->type_info >> 1);
}

static bool UBSAN_NOSAN is_inline_int(struct type_descriptor* type) {
    unsigned inline_bits = sizeof(unsigned long) * 8;
    unsigned bits        = type_bit_width(type);

    return bits <= inline_bits;
}

static __int128 UBSAN_NOSAN get_signed_val(struct type_descriptor* type, void* val) {
    if (is_inline_int(type)) {
        unsigned      extra_bits = sizeof(__int128) * 8 - type_bit_width(type);
        unsigned long ulong_val  = (unsigned long)val;

        return ((__int128)ulong_val) << extra_bits >> extra_bits;
    }

    if (type_bit_width(type) == 64) return *(int64_t*)val;

    return *(__int128*)val;
}

static bool UBSAN_NOSAN val_is_negative(struct type_descriptor* type, void* val) {
    return type_is_signed(type) && get_signed_val(type, val) < 0;
}

static unsigned __int128 UBSAN_NOSAN get_unsigned_val(struct type_descriptor* type, void* val) {
    if (is_inline_int(type)) return (unsigned long)val;

    if (type_bit_width(type) == 64) return *(uint64_t*)val;

    return *(unsigned __int128*)val;
}

static void UBSAN_NOSAN val_to_string(char* str, size_t size, struct type_descriptor* type,
                                      void* value) {
    if (type_is_int(type)) {
        if (type_bit_width(type) == 128) {
            unsigned __int128 val = get_unsigned_val(type, value);

            scnprintf(str, size, "0x%08x%08x%08x%08x", (uint32_t)(val >> 96), (uint32_t)(val >> 64),
                      (uint32_t)(val >> 32), (uint32_t)(val));
        } else if (type_is_signed(type)) {
            scnprintf(str, size, "%lld", (int64_t)get_signed_val(type, value));
        } else {
            scnprintf(str, size, "%llu", (uint64_t)get_unsigned_val(type, value));
        }
    }
}

static void UBSAN_NOSAN ubsan_prologue(struct source_location* loc, const char* reason) {
    in_ubsan++;

    printf(CUT_HERE);

    printf("UBSAN: %s in %s:%d:%d\n", reason, loc->file_name, loc->line & LINE_MASK,
           loc->column & COLUMN_MASK);
}

static void UBSAN_NOSAN ubsan_epilogue(void) {
    printf("---[ end trace ]---\n");

    in_ubsan--;
    abort();
}

static void UBSAN_NOSAN handle_overflow(struct overflow_data* data, void* lhs, void* rhs, char op) {

    struct type_descriptor* type = data->type;
    char                    lhs_val_str[VALUE_LENGTH];
    char                    rhs_val_str[VALUE_LENGTH];

    if (suppress_report(&data->location)) return;

    ubsan_prologue(&data->location,
                   type_is_signed(type) ? "signed-integer-overflow" : "unsigned-integer-overflow");

    val_to_string(lhs_val_str, sizeof(lhs_val_str), type, lhs);
    val_to_string(rhs_val_str, sizeof(rhs_val_str), type, rhs);
    printf("%s %c %s cannot be represented in type %s\n", lhs_val_str, op, rhs_val_str,
           type->type_name);

    ubsan_epilogue();
}

void UBSAN_NOSAN __ubsan_handle_add_overflow(void* data, void* lhs, void* rhs) {
    handle_overflow((struct overflow_data*)data, lhs, rhs, '+');
}
void UBSAN_NOSAN __ubsan_handle_sub_overflow(void* data, void* lhs, void* rhs) {
    handle_overflow((struct overflow_data*)data, lhs, rhs, '-');
}
void UBSAN_NOSAN __ubsan_handle_mul_overflow(void* data, void* lhs, void* rhs) {
    handle_overflow((struct overflow_data*)data, lhs, rhs, '*');
}
void UBSAN_NOSAN __ubsan_handle_pointer_overflow(void* data, void* lhs, void* rhs) {
    handle_overflow((struct overflow_data*)data, lhs, rhs, '&');
}
void UBSAN_NOSAN __ubsan_handle_negate_overflow(void* _data, void* old_val) {
    struct overflow_data* data = (struct overflow_data*)_data;
    char                  old_val_str[VALUE_LENGTH];

    if (suppress_report(&data->location)) return;

    ubsan_prologue(&data->location, "negation-overflow");

    val_to_string(old_val_str, sizeof(old_val_str), data->type, old_val);

    printf("negation of %s cannot be represented in type %s:\n", old_val_str,
           data->type->type_name);

    ubsan_epilogue();
}
void UBSAN_NOSAN __ubsan_handle_divrem_overflow(void* _data, void* lhs, void* rhs) {
    (void)lhs;
    struct overflow_data* data = (struct overflow_data*)_data;
    char                  rhs_val_str[VALUE_LENGTH];

    if (suppress_report(&data->location)) return;

    ubsan_prologue(&data->location, "division-overflow");

    val_to_string(rhs_val_str, sizeof(rhs_val_str), data->type, rhs);

    if (type_is_signed(data->type) && get_signed_val(data->type, rhs) == -1)
        printf("division of %s by -1 cannot be represented in type %s\n", rhs_val_str,
               data->type->type_name);
    else
        printf("division by zero\n");

    ubsan_epilogue();
}
static void UBSAN_NOSAN handle_null_ptr_deref(struct type_mismatch_data_common* data) {
    if (suppress_report(data->location)) return;

    ubsan_prologue(data->location, "null-ptr-deref");

    printf("%s null pointer of type %s\n", type_check_kinds[data->type_check_kind],
           data->type->type_name);

    ubsan_epilogue();
}

static void UBSAN_NOSAN handle_misaligned_access(struct type_mismatch_data_common* data,
                                                 unsigned long                     ptr) {
    if (suppress_report(data->location)) return;

    ubsan_prologue(data->location, "misaligned-access");

    printf("%s misaligned address %p for type %s\n", type_check_kinds[data->type_check_kind],
           (void*)ptr, data->type->type_name);
    printf("which requires %ld byte alignment\n", data->alignment);

    ubsan_epilogue();
}

static void UBSAN_NOSAN handle_object_size_mismatch(struct type_mismatch_data_common* data,
                                                    unsigned long                     ptr) {
    if (suppress_report(data->location)) return;

    ubsan_prologue(data->location, "object-size-mismatch");
    printf("%s address %p with insufficient space\n", type_check_kinds[data->type_check_kind],
           (void*)ptr);
    printf("for an object of type %s\n", data->type->type_name);
    ubsan_epilogue();
}
static void UBSAN_NOSAN ubsan_type_mismatch_common(struct type_mismatch_data_common* data,
                                                   unsigned long                     ptr) {
    if (!ptr)
        handle_null_ptr_deref(data);
    else if (data->alignment && !IS_ALIGNED(ptr, data->alignment))
        handle_misaligned_access(data, ptr);
    else
        handle_object_size_mismatch(data, ptr);
}
// void __ubsan_handle_implicit_conversion(void* _data, void* lhs, void* rhs) {}
void UBSAN_NOSAN __ubsan_handle_type_mismatch(struct type_mismatch_data* data, void* ptr) {
    struct type_mismatch_data_common common_data = {.location        = &data->location,
                                                    .type            = data->type,
                                                    .alignment       = data->alignment,
                                                    .type_check_kind = data->type_check_kind};
    ubsan_type_mismatch_common(&common_data, (unsigned long)ptr);
}
void UBSAN_NOSAN __ubsan_handle_type_mismatch_v1(void* _data, void* ptr) {
    struct type_mismatch_data_v1*    data        = (struct type_mismatch_data_v1*)_data;
    struct type_mismatch_data_common common_data = {.location        = &data->location,
                                                    .type            = data->type,
                                                    .alignment       = 1UL << data->log_alignment,
                                                    .type_check_kind = data->type_check_kind};

    ubsan_type_mismatch_common(&common_data, (unsigned long)ptr);
}
void UBSAN_NOSAN __ubsan_handle_out_of_bounds(void* _data, void* index) {
    struct out_of_bounds_data* data = (struct out_of_bounds_data*)_data;
    char                       index_str[VALUE_LENGTH];

    if (suppress_report(&data->location)) return;

    ubsan_prologue(&data->location, "array-index-out-of-bounds");

    val_to_string(index_str, sizeof(index_str), data->index_type, index);
    printf("index %s is out of range for type %s\n", index_str, data->array_type->type_name);
    ubsan_epilogue();
}
void UBSAN_NOSAN __ubsan_handle_shift_out_of_bounds(void* _data, void* lhs, void* rhs) {
    struct shift_out_of_bounds_data* data     = (struct shift_out_of_bounds_data*)_data;
    struct type_descriptor*          rhs_type = data->rhs_type;
    struct type_descriptor*          lhs_type = data->lhs_type;
    char                             rhs_str[VALUE_LENGTH];
    char                             lhs_str[VALUE_LENGTH];

    if (suppress_report(&data->location)) return;

    ubsan_prologue(&data->location, "shift-out-of-bounds");

    val_to_string(rhs_str, sizeof(rhs_str), rhs_type, rhs);
    val_to_string(lhs_str, sizeof(lhs_str), lhs_type, lhs);

    if (val_is_negative(rhs_type, rhs))
        printf("shift exponent %s is negative\n", rhs_str);

    else if (get_unsigned_val(rhs_type, rhs) >= type_bit_width(lhs_type))
        printf("shift exponent %s is too large for %u-bit type %s\n", rhs_str,
               type_bit_width(lhs_type), lhs_type->type_name);
    else if (val_is_negative(lhs_type, lhs))
        printf("left shift of negative value %s\n", lhs_str);
    else
        printf("left shift of %s by %s places cannot be"
               " represented in type %s\n",
               lhs_str, rhs_str, lhs_type->type_name);

    ubsan_epilogue();
}
void UBSAN_NOSAN __ubsan_handle_builtin_unreachable(void* _data) {
    struct unreachable_data* data = (struct unreachable_data*)_data;
    ubsan_prologue(&data->location, "unreachable");
    printf("calling __builtin_unreachable()\n");
    ubsan_epilogue();
}
void UBSAN_NOSAN __ubsan_handle_load_invalid_value(void* _data, void* val) {
    struct invalid_value_data* data = (struct invalid_value_data*)_data;
    char                       val_str[VALUE_LENGTH];

    if (suppress_report(&data->location)) return;

    ubsan_prologue(&data->location, "invalid-load");

    val_to_string(val_str, sizeof(val_str), data->type, val);

    printf("load of value %s is not a valid value for type %s\n", val_str, data->type->type_name);

    ubsan_epilogue();
}
void UBSAN_NOSAN __ubsan_handle_alignment_assumption(void* _data, unsigned long ptr,
                                                     unsigned long align, unsigned long offset) {
    struct alignment_assumption_data* data = (struct alignment_assumption_data*)_data;
    unsigned long                     real_ptr;

    if (suppress_report(&data->location)) return;

    ubsan_prologue(&data->location, "alignment-assumption");

    if (offset)
        printf("assumption of %lu byte alignment (with offset of %lu byte) for "
               "pointer of type %s "
               "failed",
               align, offset, data->type->type_name);
    else
        printf("assumption of %lu byte alignment for pointer of type %s failed", align,
               data->type->type_name);

    real_ptr = ptr - offset;
    printf("%saddress is %lu aligned, misalignment offset is %lu bytes", offset ? "offset " : "",
           BIT(real_ptr ? __ffs(real_ptr) : 0), real_ptr & (align - 1));

    ubsan_epilogue();
}
void UBSAN_NOSAN __ubsan_handle_nonnull_arg(void* _data) {
    struct nonnull_arg_data* data = (struct nonnull_arg_data*)_data;
    ubsan_prologue(&data->location, "nonnull-argument");
    printf("null pointer passed to nonnull argument defined at ");
    printf("%s:%d:%d\n", data->attr_location.file_name, data->attr_location.line & LINE_MASK,
           data->attr_location.column & COLUMN_MASK);
    ubsan_epilogue();
}
void UBSAN_NOSAN __ubsan_handle_nonnull_return_v1(nonnull_return_data_v1* data,
                                                  source_location*        loc) {
    ubsan_prologue(loc, "nonnull-return");
    printf("%s:%d:%d\n", data->location.file_name, data->location.line & LINE_MASK,
           data->location.column & COLUMN_MASK);
    ubsan_epilogue();
}
void UBSAN_NOSAN __ubsan_handle_float_cast_overflow(overflow_data* data, void* from) {
    char old_val_str[VALUE_LENGTH];
    if (suppress_report(&data->location)) return;
    ubsan_prologue(&data->location, "negation-overflow");
    val_to_string(old_val_str, sizeof(old_val_str), data->type, from);
    printf("%s is outside the range of representable values of type %s\n", old_val_str,
           data->type->type_name);
    ubsan_epilogue();
}
void UBSAN_NOSAN __ubsan_handle_function_type_mismatch(function_type_mismatch_data* data,
                                                       void*                        from) {
    char old_val_str[VALUE_LENGTH];
    if (suppress_report(&data->location)) return;
    ubsan_prologue(&data->location, "negation-overflow");
    val_to_string(old_val_str, sizeof(old_val_str), &data->type, from);
    printf("call to function %s through pointer to incorrect function type %s\n", old_val_str,
           data->type.type_name);
    ubsan_epilogue();
}
