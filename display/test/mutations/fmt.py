"""Mutations of display/components/core/fmt.c, see ../redproof.py."""

F = "components/core/fmt.c"
T = "test_fmt"

DIGITS = ("\tdo\n\t{\n\t\treversed[length++] = (char)('0' + number % 10);\n\t\tnumber /= 10;\n\t}\n"
          "\twhile(number > 0);\n")
ROUND = "\tnumber = (uint64_t)(magnitude * scales[decimals]);\n\tnumber = (number + 1) / 2;\n"
HALVES = "\tnumber = (number + 1) / 2;"
# true where the value lies beyond the count of halves, false where it is that count exactly
BEYOND = "(double)number != magnitude * scales[decimals]"
LIMIT = "\tif(!(magnitude < MAGNITUDE_LIMIT)) return false;"
BREAK = ("\t\t\tfirst = true;\n\t\t\tcontinue;\n\t\t}\n\n"
         "\t\t// One space between two words, however many breaks there were, and none before the first word.\n"
         "\t\t// Breaks at the end are never followed by a word and so leave nothing. There is room for the\n"
         "\t\t// space: it takes the place kept for the terminating zero.\n"
         "\t\tif(first && length > 0) out[length++] = ' ';\n")

MUTATIONS = [
    # fmt_number: what is refused
    ("fmt_number_zero_size_writes", T, F,
     "\tif(size == 0) return false;\n\tout[0] = '\\0';\n", "\tout[0] = '\\0';\n\tif(size == 0) return false;\n"),
    ("fmt_number_refused_not_emptied", T, F,
     "\tif(size == 0) return false;\n\tout[0] = '\\0';\n", "\tif(size == 0) return false;\n"),
    ("fmt_number_limit_itself_accepted", T, F, "\tif(!(magnitude < MAGNITUDE_LIMIT)) return false;", "\tif(!(magnitude <= MAGNITUDE_LIMIT)) return false;"),
    ("fmt_number_limit_ten_times_higher", T, F, "#define MAGNITUDE_LIMIT 1e12", "#define MAGNITUDE_LIMIT 1e13"),
    ("fmt_number_limit_only_for_positive", T, F, "\tif(!(magnitude < MAGNITUDE_LIMIT)) return false;", "\tif(!(value < MAGNITUDE_LIMIT)) return false;"),
    ("fmt_number_nan_printed_as_zero", T, F,
     "\tmagnitude = value < 0 ? -value : value;", "\tmagnitude = value < 0 ? -value : value > 0 ? value : 0;"),
    ("fmt_number_limit_not_checked_with_1_decimal", T, F, LIMIT, "\tif(!(magnitude < MAGNITUDE_LIMIT) && decimals != 1) return false;"),
    ("fmt_number_limit_not_checked_with_2_decimals", T, F, LIMIT, "\tif(!(magnitude < MAGNITUDE_LIMIT) && decimals != 2) return false;"),
    ("fmt_number_negative_limit_accepted_with_2_decimals", T, F,
     LIMIT, "\tif(!(magnitude < MAGNITUDE_LIMIT) && !(decimals == 2 && value == -MAGNITUDE_LIMIT)) return false;"),

    # fmt_number: decimals
    ("fmt_number_negative_decimals_count_as_one", T, F, "\tif(decimals < 0) decimals = 0;", "\tif(decimals < 0) decimals = 1;"),
    ("fmt_number_many_decimals_count_as_none", T, F,
     "\tif(decimals > DECIMALS_MAX) decimals = DECIMALS_MAX;", "\tif(decimals > DECIMALS_MAX) decimals = 0;"),
    ("fmt_number_three_decimals_count_as_two", T, F,
     "\tif(decimals > DECIMALS_MAX) decimals = DECIMALS_MAX;", "\tif(decimals >= DECIMALS_MAX) decimals = DECIMALS_MAX - 1;"),
    ("fmt_number_one_decimal_less", T, F, "\tfor(int i = 0; i < decimals; i++)\n\t{\n\t\treversed", "\tfor(int i = 1; i < decimals; i++)\n\t{\n\t\treversed"),
    ("fmt_number_comma_without_decimals", T, F, "\tif(decimals > 0) reversed[length++] = ',';", "\treversed[length++] = ',';"),
    ("fmt_number_no_comma_with_one_decimal", T, F, "\tif(decimals > 0) reversed[length++] = ',';", "\tif(decimals > 1) reversed[length++] = ',';"),
    ("fmt_number_decimal_point", T, F, "reversed[length++] = ',';", "reversed[length++] = '.';"),
    ("fmt_number_scale_wrong", T, F, "{2, 20, 200, 2000};", "{2, 20, 200, 200};"),

    # fmt_number: rounding
    ("fmt_number_not_rounded", T, F, HALVES, "\tnumber = number / 2;"),
    ("fmt_number_half_rounds_down", T, F, HALVES, "\tnumber = (number + (" + BEYOND + ")) / 2;"),
    ("fmt_number_half_rounds_to_even", T, F,
     HALVES, "\tnumber = number / 2 + (number % 2 == 1 && (" + BEYOND + " || number / 2 % 2 == 1));"),
    ("fmt_number_negative_half_rounds_up", T, F,
     HALVES, "\tnumber = number / 2 + (number % 2 == 1 && (" + BEYOND + " || value > 0));"),
    ("fmt_number_rounded_by_adding_half", T, F,
     ROUND, "\tnumber = (uint64_t)(magnitude * scales[decimals] + 1);\n\tnumber = number / 2;\n"),
    ("fmt_number_always_rounded_up", T, F, HALVES, "\tnumber = number / 2 + ((double)(number / 2 * 2) != magnitude * scales[decimals]);"),
    ("fmt_number_half_of_large_number_rounds_down", T, F,
     HALVES, "\tnumber = (number + (number > 20000 && number < 1000000000000ull && !(" + BEYOND + ") ? 0 : 1)) / 2;"),
    ("fmt_number_half_of_large_number_rounds_to_even", T, F,
     HALVES, "\tnumber = number / 2 + (number % 2 == 1 && (number < 20000 || number > 1000000000000ull || " + BEYOND + " || number / 2 % 2 == 1));"),
    # what a compiler makes of "product - whole part >= 0.5" when it merges the product into the difference
    ("fmt_number_half_taken_from_unrounded_product", T, F,
     ROUND,
     "\tnumber = (uint64_t)(magnitude * scales[decimals]) / 2;\n"
     "\tif(__builtin_fma(magnitude, scales[decimals], -(double)(2 * number)) >= 1) number++;\n"),
    ("fmt_number_halves_not_halved", T, F, "{2, 20, 200, 2000};", "{1, 10, 100, 1000};"),
    # the next double above each scale: the product of a number just below a half becomes the half
    ("fmt_number_scale_of_0_decimals_one_step_higher", T, F, "{2, 20, 200, 2000};", "{2.0000000000000004, 20, 200, 2000};"),
    ("fmt_number_scale_of_1_decimal_one_step_higher", T, F, "{2, 20, 200, 2000};", "{2, 20.000000000000004, 200, 2000};"),
    ("fmt_number_scale_of_2_decimals_one_step_higher", T, F, "{2, 20, 200, 2000};", "{2, 20, 200.00000000000003, 2000};"),
    ("fmt_number_scale_of_3_decimals_one_step_higher", T, F, "{2, 20, 200, 2000};", "{2, 20, 200, 2000.0000000000002};"),

    # fmt_number: sign
    ("fmt_number_minus_zero", T, F, "\tnegative = value < 0 && number != 0;", "\tnegative = value < 0;"),
    ("fmt_number_no_sign", T, F, "\tnegative = value < 0 && number != 0;", "\tnegative = false;"),
    ("fmt_number_sign_by_whole_part", T, F, "\tnegative = value < 0 && number != 0;", "\tnegative = value <= -1;"),

    # fmt_number: digits
    ("fmt_number_zero_has_no_digit", T, F,
     DIGITS, "\twhile(number > 0)\n\t{\n\t\treversed[length++] = (char)('0' + number % 10);\n\t\tnumber /= 10;\n\t}\n"),
    ("fmt_number_thousands_separator", T, F,
     DIGITS,
     "\tfor(int i = 1; ; i++)\n\t{\n\t\treversed[length++] = (char)('0' + number % 10);\n\t\tnumber /= 10;\n"
     "\t\tif(number == 0) break;\n\t\tif(i % 3 == 0) reversed[length++] = '.';\n\t}\n"),
    ("fmt_number_digits_reversed", T, F, "out[i] = reversed[length - 1 - i];", "out[i] = reversed[i];"),
    ("fmt_number_not_terminated", T, F, "\tout[length] = '\\0';\n\treturn true;\n", "\treturn true;\n"),

    # fmt_number: room
    ("fmt_number_overflows_by_one", T, F, "\tif(length + 1 > size) return false;", "\tif(length > size) return false;"),
    ("fmt_number_exact_fit_refused", T, F, "\tif(length + 1 > size) return false;", "\tif(length + 2 > size) return false;"),
    ("fmt_number_room_not_checked", T, F, "\tif(length + 1 > size) return false;\n", ""),
    ("fmt_number_cut_instead_of_refused", T, F,
     "\tif(length + 1 > size) return false;", "\tif(length + 1 > size) length = size - 1;"),

    ("fmt_number_room_not_checked_for_9_bytes", T, F, "\tif(length + 1 > size) return false;", "\tif(length + 1 > size && length != 9) return false;"),
    ("fmt_number_exact_fit_refused_for_10_bytes", T, F,
     "\tif(length + 1 > size) return false;", "\tif(length + 1 > size - (length == 10 && size > 0)) return false;"),
    ("fmt_number_refused_for_one_size", T, F, "\tif(length + 1 > size) return false;", "\tif(length + 1 > size || size == 33) return false;"),

    # fmt_label: words
    ("fmt_label_underscore_is_no_break", T, F, "\t\tif(c == '_' || c == '@')", "\t\tif(c == '@')"),
    ("fmt_label_at_is_no_break", T, F, "\t\tif(c == '_' || c == '@')", "\t\tif(c == '_')"),
    ("fmt_label_space_is_a_break", T, F, "\t\tif(c == '_' || c == '@')", "\t\tif(c == '_' || c == '@' || c == ' ')"),
    ("fmt_label_space_at_the_start", T, F, "\t\tif(first && length > 0) out[length++] = ' ';", "\t\tif(first) out[length++] = ' ';"),
    ("fmt_label_space_at_the_end", T, F,
     BREAK, "\t\t\tif(!first && length + 1 < size) out[length++] = ' ';\n\t\t\tfirst = true;\n\t\t\tcontinue;\n\t\t}\n"),
    ("fmt_label_every_break_a_space", T, F,
     BREAK, "\t\t\tif(length > 0 && length + 1 < size) out[length++] = ' ';\n\t\t\tfirst = true;\n\t\t\tcontinue;\n\t\t}\n"),
    ("fmt_label_words_not_separated", T, F, "\t\tif(first && length > 0) out[length++] = ' ';\n", ""),
    ("fmt_label_underscore_kept", T, F, "\t\tif(first && length > 0) out[length++] = ' ';", "\t\tif(first && length > 0) out[length++] = '_';"),

    # fmt_label: letters
    ("fmt_label_first_letter_not_raised", T, F, "\t\tif(first && c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');\n", ""),
    ("fmt_label_other_letters_not_lowered", T, F, "\t\tif(!first && c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');\n", ""),
    ("fmt_label_only_first_word_raised", T, F, "\t\tif(first && c >= 'a' && c <= 'z')", "\t\tif(length == 0 && c >= 'a' && c <= 'z')"),
    ("fmt_label_later_words_not_raised", T, F, "\t\tif(first && c >= 'a' && c <= 'z')", "\t\tif(first && length < 12 && c >= 'a' && c <= 'z')"),
    ("fmt_label_later_letters_not_lowered", T, F, "\t\tif(!first && c >= 'A' && c <= 'Z')", "\t\tif(!first && length < 12 && c >= 'A' && c <= 'Z')"),
    ("fmt_label_words_from_40_bytes_not_raised", T, F, "\t\tif(first && c >= 'a' && c <= 'z')", "\t\tif(first && length < 40 && c >= 'a' && c <= 'z')"),
    ("fmt_label_letters_from_40_bytes_not_lowered", T, F, "\t\tif(!first && c >= 'A' && c <= 'Z')", "\t\tif(!first && length < 40 && c >= 'A' && c <= 'Z')"),
    ("fmt_label_all_letters_raised", T, F,
     "\t\tif(!first && c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');", "\t\tif(!first && c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');"),
    ("fmt_label_a_not_raised", T, F, "\t\tif(first && c >= 'a' && c <= 'z')", "\t\tif(first && c > 'a' && c <= 'z')"),
    ("fmt_label_z_not_raised", T, F, "\t\tif(first && c >= 'a' && c <= 'z')", "\t\tif(first && c >= 'a' && c < 'z')"),
    ("fmt_label_sign_before_a_raised", T, F, "\t\tif(first && c >= 'a' && c <= 'z')", "\t\tif(first && c >= '`' && c <= 'z')"),
    ("fmt_label_sign_behind_z_raised", T, F, "\t\tif(first && c >= 'a' && c <= 'z')", "\t\tif(first && c >= 'a' && c <= '{')"),
    ("fmt_label_upper_a_not_lowered", T, F, "\t\tif(!first && c >= 'A' && c <= 'Z')", "\t\tif(!first && c > 'A' && c <= 'Z')"),
    ("fmt_label_upper_z_not_lowered", T, F, "\t\tif(!first && c >= 'A' && c <= 'Z')", "\t\tif(!first && c >= 'A' && c < 'Z')"),
    ("fmt_label_sign_behind_upper_z_lowered", T, F, "\t\tif(!first && c >= 'A' && c <= 'Z')", "\t\tif(!first && c >= 'A' && c <= '[')"),
    ("fmt_label_high_bit_lost", T, F, "\t\tchar c = *name;", "\t\tchar c = (char)(*name & 0x7F);"),
    ("fmt_label_letter_behind_digit_raised", T, F, "\t\tfirst = false;\n", "\t\tif(c < '0' || c > '9') first = false;\n"),

    # fmt_label: room
    ("fmt_label_zero_size_writes", T, F, "\tif(size == 0) return false;\n\n\tfor(; *name != '\\0'; name++)", "\tfor(; *name != '\\0'; name++)"),
    ("fmt_label_overflows_by_one", T, F, "\t\tif(length + 1 >= size) break;", "\t\tif(length >= size) break;"),
    ("fmt_label_exact_fit_refused", T, F, "\t\tif(length + 1 >= size) break;", "\t\tif(length + 2 >= size) break;"),
    ("fmt_label_room_not_checked_at_20_bytes", T, F, "\t\tif(length + 1 >= size) break;", "\t\tif(length + 1 >= size && length != 20) break;"),
    ("fmt_label_exact_fit_refused_at_25_bytes", T, F, "\t\tif(length + 1 >= size) break;", "\t\tif(length + 1 >= size - (length == 25)) break;"),
    ("fmt_label_room_not_checked_from_60_bytes", T, F, "\t\tif(length + 1 >= size) break;", "\t\tif(length + 1 >= size && size < 60) break;"),
    ("fmt_label_refused_reported_as_fitting_from_61_bytes", T, F, "\treturn *name == '\\0';", "\treturn *name == '\\0' || size > 60;"),
    ("fmt_label_refused_not_emptied_in_large_buffer", T, F,
     "\tif(*name != '\\0') length = 0;\n", "\tif(*name != '\\0') length = size > 30 ? 1 : 0;\n"),
    ("fmt_label_refused_not_emptied", T, F, "\tif(*name != '\\0') length = 0;\n", ""),
    ("fmt_label_cut_instead_of_refused", T, F, "\treturn *name == '\\0';", "\treturn true;"),
    ("fmt_label_cut_and_reported_as_fitting", T, F,
     "\tif(*name != '\\0') length = 0;\n\tout[length] = '\\0';\n\treturn *name == '\\0';", "\tout[length] = '\\0';\n\treturn true;"),
]
