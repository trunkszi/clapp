/**
 * \file
 * \brief Display width, greedy line wrap, escape stripping, terminal-width lookup.
 */

#pragma once

#include <clapp/lex/os_str.hpp>
#include <clapp/output/styled_str.hpp>
#include <clapp/util/str.hpp>

#include <array>
#include <cstddef>
#include <cstdint>  // IWYU pragma: keep
#include <cstdlib>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#if !defined(_WIN32)
// Two syscall headers rather than anything from <iostream>: detect_terminal_width()
// needs exactly `ioctl`, `TIOCGWINSZ`, `struct winsize` and `STDOUT_FILENO`, and
// <clapp/lex/raw_args.hpp> already pulls <unistd.h> into every translation unit
// that includes any clapp header, so the marginal cost here is <sys/ioctl.h>.
#    include <sys/ioctl.h>
#    include <unistd.h>
#endif

namespace clapp {

    namespace detail {

        /** \brief Inclusive code-point range for width tables. */
        struct code_point_range {
            std::uint32_t lowest  = 0;  /**< First code point in the range, inclusive. */
            std::uint32_t highest = 0;  /**< Last code point in the range, inclusive. */

            /** \brief Compare both inclusive range bounds. */
            [[nodiscard]] constexpr bool
            operator==(const code_point_range&) const noexcept = default;
        };

        /**
         * \brief East Asian Width W/F ranges (Unicode 16.0.0); sorted, non-overlapping.
         */
        inline constexpr std::array<code_point_range, 122> east_asian_wide_ranges{{
                {0x01100, 0x0115F}, {0x0231A, 0x0231B}, {0x02329, 0x0232A}, {0x023E9, 0x023EC},
                {0x023F0, 0x023F0}, {0x023F3, 0x023F3}, {0x025FD, 0x025FE}, {0x02614, 0x02615},
                {0x02630, 0x02637}, {0x02648, 0x02653}, {0x0267F, 0x0267F}, {0x0268A, 0x0268F},
                {0x02693, 0x02693}, {0x026A1, 0x026A1}, {0x026AA, 0x026AB}, {0x026BD, 0x026BE},
                {0x026C4, 0x026C5}, {0x026CE, 0x026CE}, {0x026D4, 0x026D4}, {0x026EA, 0x026EA},
                {0x026F2, 0x026F3}, {0x026F5, 0x026F5}, {0x026FA, 0x026FA}, {0x026FD, 0x026FD},
                {0x02705, 0x02705}, {0x0270A, 0x0270B}, {0x02728, 0x02728}, {0x0274C, 0x0274C},
                {0x0274E, 0x0274E}, {0x02753, 0x02755}, {0x02757, 0x02757}, {0x02795, 0x02797},
                {0x027B0, 0x027B0}, {0x027BF, 0x027BF}, {0x02B1B, 0x02B1C}, {0x02B50, 0x02B50},
                {0x02B55, 0x02B55}, {0x02E80, 0x02E99}, {0x02E9B, 0x02EF3}, {0x02F00, 0x02FD5},
                {0x02FF0, 0x0303E}, {0x03041, 0x03096}, {0x03099, 0x030FF}, {0x03105, 0x0312F},
                {0x03131, 0x0318E}, {0x03190, 0x031E5}, {0x031EF, 0x0321E}, {0x03220, 0x03247},
                {0x03250, 0x0A48C}, {0x0A490, 0x0A4C6}, {0x0A960, 0x0A97C}, {0x0AC00, 0x0D7A3},
                {0x0F900, 0x0FAFF}, {0x0FE10, 0x0FE19}, {0x0FE30, 0x0FE52}, {0x0FE54, 0x0FE66},
                {0x0FE68, 0x0FE6B}, {0x0FF01, 0x0FF60}, {0x0FFE0, 0x0FFE6}, {0x16FE0, 0x16FE4},
                {0x16FF0, 0x16FF1}, {0x17000, 0x187F7}, {0x18800, 0x18CD5}, {0x18CFF, 0x18D08},
                {0x1AFF0, 0x1AFF3}, {0x1AFF5, 0x1AFFB}, {0x1AFFD, 0x1AFFE}, {0x1B000, 0x1B122},
                {0x1B132, 0x1B132}, {0x1B150, 0x1B152}, {0x1B155, 0x1B155}, {0x1B164, 0x1B167},
                {0x1B170, 0x1B2FB}, {0x1D300, 0x1D356}, {0x1D360, 0x1D376}, {0x1F004, 0x1F004},
                {0x1F0CF, 0x1F0CF}, {0x1F18E, 0x1F18E}, {0x1F191, 0x1F19A}, {0x1F200, 0x1F202},
                {0x1F210, 0x1F23B}, {0x1F240, 0x1F248}, {0x1F250, 0x1F251}, {0x1F260, 0x1F265},
                {0x1F300, 0x1F320}, {0x1F32D, 0x1F335}, {0x1F337, 0x1F37C}, {0x1F37E, 0x1F393},
                {0x1F3A0, 0x1F3CA}, {0x1F3CF, 0x1F3D3}, {0x1F3E0, 0x1F3F0}, {0x1F3F4, 0x1F3F4},
                {0x1F3F8, 0x1F43E}, {0x1F440, 0x1F440}, {0x1F442, 0x1F4FC}, {0x1F4FF, 0x1F53D},
                {0x1F54B, 0x1F54E}, {0x1F550, 0x1F567}, {0x1F57A, 0x1F57A}, {0x1F595, 0x1F596},
                {0x1F5A4, 0x1F5A4}, {0x1F5FB, 0x1F64F}, {0x1F680, 0x1F6C5}, {0x1F6CC, 0x1F6CC},
                {0x1F6D0, 0x1F6D2}, {0x1F6D5, 0x1F6D7}, {0x1F6DC, 0x1F6DF}, {0x1F6EB, 0x1F6EC},
                {0x1F6F4, 0x1F6FC}, {0x1F7E0, 0x1F7EB}, {0x1F7F0, 0x1F7F0}, {0x1F90C, 0x1F93A},
                {0x1F93C, 0x1F945}, {0x1F947, 0x1F9FF}, {0x1FA70, 0x1FA7C}, {0x1FA80, 0x1FA89},
                {0x1FA8F, 0x1FAC6}, {0x1FACE, 0x1FADC}, {0x1FADF, 0x1FAE9}, {0x1FAF0, 0x1FAF8},
                {0x20000, 0x2FFFD}, {0x30000, 0x3FFFD},
        }};

        /**
         * \brief Zero-width code points (Mn/Me/Cf, Hangul Jamo; Unicode 16.0.0).
         * \note C0/C1 handled in char_display_width (not tabled — every ASCII byte).
         */
        inline constexpr std::array<code_point_range, 368> zero_width_ranges{{
                {0x00300, 0x0036F}, {0x00483, 0x00489}, {0x00591, 0x005BD}, {0x005BF, 0x005BF},
                {0x005C1, 0x005C2}, {0x005C4, 0x005C5}, {0x005C7, 0x005C7}, {0x00600, 0x00605},
                {0x00610, 0x0061A}, {0x0061C, 0x0061C}, {0x0064B, 0x0065F}, {0x00670, 0x00670},
                {0x006D6, 0x006DD}, {0x006DF, 0x006E4}, {0x006E7, 0x006E8}, {0x006EA, 0x006ED},
                {0x0070F, 0x0070F}, {0x00711, 0x00711}, {0x00730, 0x0074A}, {0x007A6, 0x007B0},
                {0x007EB, 0x007F3}, {0x007FD, 0x007FD}, {0x00816, 0x00819}, {0x0081B, 0x00823},
                {0x00825, 0x00827}, {0x00829, 0x0082D}, {0x00859, 0x0085B}, {0x00890, 0x00891},
                {0x00897, 0x0089F}, {0x008CA, 0x00902}, {0x0093A, 0x0093A}, {0x0093C, 0x0093C},
                {0x00941, 0x00948}, {0x0094D, 0x0094D}, {0x00951, 0x00957}, {0x00962, 0x00963},
                {0x00981, 0x00981}, {0x009BC, 0x009BC}, {0x009C1, 0x009C4}, {0x009CD, 0x009CD},
                {0x009E2, 0x009E3}, {0x009FE, 0x009FE}, {0x00A01, 0x00A02}, {0x00A3C, 0x00A3C},
                {0x00A41, 0x00A42}, {0x00A47, 0x00A48}, {0x00A4B, 0x00A4D}, {0x00A51, 0x00A51},
                {0x00A70, 0x00A71}, {0x00A75, 0x00A75}, {0x00A81, 0x00A82}, {0x00ABC, 0x00ABC},
                {0x00AC1, 0x00AC5}, {0x00AC7, 0x00AC8}, {0x00ACD, 0x00ACD}, {0x00AE2, 0x00AE3},
                {0x00AFA, 0x00AFF}, {0x00B01, 0x00B01}, {0x00B3C, 0x00B3C}, {0x00B3F, 0x00B3F},
                {0x00B41, 0x00B44}, {0x00B4D, 0x00B4D}, {0x00B55, 0x00B56}, {0x00B62, 0x00B63},
                {0x00B82, 0x00B82}, {0x00BC0, 0x00BC0}, {0x00BCD, 0x00BCD}, {0x00C00, 0x00C00},
                {0x00C04, 0x00C04}, {0x00C3C, 0x00C3C}, {0x00C3E, 0x00C40}, {0x00C46, 0x00C48},
                {0x00C4A, 0x00C4D}, {0x00C55, 0x00C56}, {0x00C62, 0x00C63}, {0x00C81, 0x00C81},
                {0x00CBC, 0x00CBC}, {0x00CBF, 0x00CBF}, {0x00CC6, 0x00CC6}, {0x00CCC, 0x00CCD},
                {0x00CE2, 0x00CE3}, {0x00D00, 0x00D01}, {0x00D3B, 0x00D3C}, {0x00D41, 0x00D44},
                {0x00D4D, 0x00D4D}, {0x00D62, 0x00D63}, {0x00D81, 0x00D81}, {0x00DCA, 0x00DCA},
                {0x00DD2, 0x00DD4}, {0x00DD6, 0x00DD6}, {0x00E31, 0x00E31}, {0x00E34, 0x00E3A},
                {0x00E47, 0x00E4E}, {0x00EB1, 0x00EB1}, {0x00EB4, 0x00EBC}, {0x00EC8, 0x00ECE},
                {0x00F18, 0x00F19}, {0x00F35, 0x00F35}, {0x00F37, 0x00F37}, {0x00F39, 0x00F39},
                {0x00F71, 0x00F7E}, {0x00F80, 0x00F84}, {0x00F86, 0x00F87}, {0x00F8D, 0x00F97},
                {0x00F99, 0x00FBC}, {0x00FC6, 0x00FC6}, {0x0102D, 0x01030}, {0x01032, 0x01037},
                {0x01039, 0x0103A}, {0x0103D, 0x0103E}, {0x01058, 0x01059}, {0x0105E, 0x01060},
                {0x01071, 0x01074}, {0x01082, 0x01082}, {0x01085, 0x01086}, {0x0108D, 0x0108D},
                {0x0109D, 0x0109D}, {0x01160, 0x011FF}, {0x0135D, 0x0135F}, {0x01712, 0x01714},
                {0x01732, 0x01733}, {0x01752, 0x01753}, {0x01772, 0x01773}, {0x017B4, 0x017B5},
                {0x017B7, 0x017BD}, {0x017C6, 0x017C6}, {0x017C9, 0x017D3}, {0x017DD, 0x017DD},
                {0x0180B, 0x0180F}, {0x01885, 0x01886}, {0x018A9, 0x018A9}, {0x01920, 0x01922},
                {0x01927, 0x01928}, {0x01932, 0x01932}, {0x01939, 0x0193B}, {0x01A17, 0x01A18},
                {0x01A1B, 0x01A1B}, {0x01A56, 0x01A56}, {0x01A58, 0x01A5E}, {0x01A60, 0x01A60},
                {0x01A62, 0x01A62}, {0x01A65, 0x01A6C}, {0x01A73, 0x01A7C}, {0x01A7F, 0x01A7F},
                {0x01AB0, 0x01ACE}, {0x01B00, 0x01B03}, {0x01B34, 0x01B34}, {0x01B36, 0x01B3A},
                {0x01B3C, 0x01B3C}, {0x01B42, 0x01B42}, {0x01B6B, 0x01B73}, {0x01B80, 0x01B81},
                {0x01BA2, 0x01BA5}, {0x01BA8, 0x01BA9}, {0x01BAB, 0x01BAD}, {0x01BE6, 0x01BE6},
                {0x01BE8, 0x01BE9}, {0x01BED, 0x01BED}, {0x01BEF, 0x01BF1}, {0x01C2C, 0x01C33},
                {0x01C36, 0x01C37}, {0x01CD0, 0x01CD2}, {0x01CD4, 0x01CE0}, {0x01CE2, 0x01CE8},
                {0x01CED, 0x01CED}, {0x01CF4, 0x01CF4}, {0x01CF8, 0x01CF9}, {0x01DC0, 0x01DFF},
                {0x0200B, 0x0200F}, {0x0202A, 0x0202E}, {0x02060, 0x02064}, {0x02066, 0x0206F},
                {0x020D0, 0x020F0}, {0x02CEF, 0x02CF1}, {0x02D7F, 0x02D7F}, {0x02DE0, 0x02DFF},
                {0x0302A, 0x0302D}, {0x03099, 0x0309A}, {0x0A66F, 0x0A672}, {0x0A674, 0x0A67D},
                {0x0A69E, 0x0A69F}, {0x0A6F0, 0x0A6F1}, {0x0A802, 0x0A802}, {0x0A806, 0x0A806},
                {0x0A80B, 0x0A80B}, {0x0A825, 0x0A826}, {0x0A82C, 0x0A82C}, {0x0A8C4, 0x0A8C5},
                {0x0A8E0, 0x0A8F1}, {0x0A8FF, 0x0A8FF}, {0x0A926, 0x0A92D}, {0x0A947, 0x0A951},
                {0x0A980, 0x0A982}, {0x0A9B3, 0x0A9B3}, {0x0A9B6, 0x0A9B9}, {0x0A9BC, 0x0A9BD},
                {0x0A9E5, 0x0A9E5}, {0x0AA29, 0x0AA2E}, {0x0AA31, 0x0AA32}, {0x0AA35, 0x0AA36},
                {0x0AA43, 0x0AA43}, {0x0AA4C, 0x0AA4C}, {0x0AA7C, 0x0AA7C}, {0x0AAB0, 0x0AAB0},
                {0x0AAB2, 0x0AAB4}, {0x0AAB7, 0x0AAB8}, {0x0AABE, 0x0AABF}, {0x0AAC1, 0x0AAC1},
                {0x0AAEC, 0x0AAED}, {0x0AAF6, 0x0AAF6}, {0x0ABE5, 0x0ABE5}, {0x0ABE8, 0x0ABE8},
                {0x0ABED, 0x0ABED}, {0x0FB1E, 0x0FB1E}, {0x0FE00, 0x0FE0F}, {0x0FE20, 0x0FE2F},
                {0x0FEFF, 0x0FEFF}, {0x0FFF9, 0x0FFFB}, {0x101FD, 0x101FD}, {0x102E0, 0x102E0},
                {0x10376, 0x1037A}, {0x10A01, 0x10A03}, {0x10A05, 0x10A06}, {0x10A0C, 0x10A0F},
                {0x10A38, 0x10A3A}, {0x10A3F, 0x10A3F}, {0x10AE5, 0x10AE6}, {0x10D24, 0x10D27},
                {0x10D69, 0x10D6D}, {0x10EAB, 0x10EAC}, {0x10EFC, 0x10EFF}, {0x10F46, 0x10F50},
                {0x10F82, 0x10F85}, {0x11001, 0x11001}, {0x11038, 0x11046}, {0x11070, 0x11070},
                {0x11073, 0x11074}, {0x1107F, 0x11081}, {0x110B3, 0x110B6}, {0x110B9, 0x110BA},
                {0x110BD, 0x110BD}, {0x110C2, 0x110C2}, {0x110CD, 0x110CD}, {0x11100, 0x11102},
                {0x11127, 0x1112B}, {0x1112D, 0x11134}, {0x11173, 0x11173}, {0x11180, 0x11181},
                {0x111B6, 0x111BE}, {0x111C9, 0x111CC}, {0x111CF, 0x111CF}, {0x1122F, 0x11231},
                {0x11234, 0x11234}, {0x11236, 0x11237}, {0x1123E, 0x1123E}, {0x11241, 0x11241},
                {0x112DF, 0x112DF}, {0x112E3, 0x112EA}, {0x11300, 0x11301}, {0x1133B, 0x1133C},
                {0x11340, 0x11340}, {0x11366, 0x1136C}, {0x11370, 0x11374}, {0x113BB, 0x113C0},
                {0x113CE, 0x113CE}, {0x113D0, 0x113D0}, {0x113D2, 0x113D2}, {0x113E1, 0x113E2},
                {0x11438, 0x1143F}, {0x11442, 0x11444}, {0x11446, 0x11446}, {0x1145E, 0x1145E},
                {0x114B3, 0x114B8}, {0x114BA, 0x114BA}, {0x114BF, 0x114C0}, {0x114C2, 0x114C3},
                {0x115B2, 0x115B5}, {0x115BC, 0x115BD}, {0x115BF, 0x115C0}, {0x115DC, 0x115DD},
                {0x11633, 0x1163A}, {0x1163D, 0x1163D}, {0x1163F, 0x11640}, {0x116AB, 0x116AB},
                {0x116AD, 0x116AD}, {0x116B0, 0x116B5}, {0x116B7, 0x116B7}, {0x1171D, 0x1171D},
                {0x1171F, 0x1171F}, {0x11722, 0x11725}, {0x11727, 0x1172B}, {0x1182F, 0x11837},
                {0x11839, 0x1183A}, {0x1193B, 0x1193C}, {0x1193E, 0x1193E}, {0x11943, 0x11943},
                {0x119D4, 0x119D7}, {0x119DA, 0x119DB}, {0x119E0, 0x119E0}, {0x11A01, 0x11A0A},
                {0x11A33, 0x11A38}, {0x11A3B, 0x11A3E}, {0x11A47, 0x11A47}, {0x11A51, 0x11A56},
                {0x11A59, 0x11A5B}, {0x11A8A, 0x11A96}, {0x11A98, 0x11A99}, {0x11C30, 0x11C36},
                {0x11C38, 0x11C3D}, {0x11C3F, 0x11C3F}, {0x11C92, 0x11CA7}, {0x11CAA, 0x11CB0},
                {0x11CB2, 0x11CB3}, {0x11CB5, 0x11CB6}, {0x11D31, 0x11D36}, {0x11D3A, 0x11D3A},
                {0x11D3C, 0x11D3D}, {0x11D3F, 0x11D45}, {0x11D47, 0x11D47}, {0x11D90, 0x11D91},
                {0x11D95, 0x11D95}, {0x11D97, 0x11D97}, {0x11EF3, 0x11EF4}, {0x11F00, 0x11F01},
                {0x11F36, 0x11F3A}, {0x11F40, 0x11F40}, {0x11F42, 0x11F42}, {0x11F5A, 0x11F5A},
                {0x13430, 0x13440}, {0x13447, 0x13455}, {0x1611E, 0x16129}, {0x1612D, 0x1612F},
                {0x16AF0, 0x16AF4}, {0x16B30, 0x16B36}, {0x16F4F, 0x16F4F}, {0x16F8F, 0x16F92},
                {0x16FE4, 0x16FE4}, {0x1BC9D, 0x1BC9E}, {0x1BCA0, 0x1BCA3}, {0x1CF00, 0x1CF2D},
                {0x1CF30, 0x1CF46}, {0x1D167, 0x1D169}, {0x1D173, 0x1D182}, {0x1D185, 0x1D18B},
                {0x1D1AA, 0x1D1AD}, {0x1D242, 0x1D244}, {0x1DA00, 0x1DA36}, {0x1DA3B, 0x1DA6C},
                {0x1DA75, 0x1DA75}, {0x1DA84, 0x1DA84}, {0x1DA9B, 0x1DA9F}, {0x1DAA1, 0x1DAAF},
                {0x1E000, 0x1E006}, {0x1E008, 0x1E018}, {0x1E01B, 0x1E021}, {0x1E023, 0x1E024},
                {0x1E026, 0x1E02A}, {0x1E08F, 0x1E08F}, {0x1E130, 0x1E136}, {0x1E2AE, 0x1E2AE},
                {0x1E2EC, 0x1E2EF}, {0x1E4EC, 0x1E4EF}, {0x1E5EE, 0x1E5EF}, {0x1E8D0, 0x1E8D6},
                {0x1E944, 0x1E94A}, {0xE0001, 0xE0001}, {0xE0020, 0xE007F}, {0xE0100, 0xE01EF},
        }};

        /**
         * \brief Whether \p code_point is in \p table (sorted, non-overlapping).
         * \param table Range table.
         * \param code_point Value to look up.
         * \return true if some range contains \p code_point.
         * \note Index bisection, not partition_point — no pointer compares in
         *       constant expressions (trap 10 / ubsan).
         */
        [[nodiscard]] constexpr bool in_range(std::span<const code_point_range> table,
                                              std::uint32_t code_point) noexcept {
            std::size_t low  = 0;
            std::size_t high = table.size();
            while (low < high) {
                const std::size_t middle      = low + (high - low) / 2;
                const code_point_range& entry = table[middle];
                if (code_point < entry.lowest) {
                    high = middle;
                } else if (code_point > entry.highest) {
                    low = middle + 1;
                } else {
                    return true;
                }
            }
            return false;
        }

        /** \brief Verify that a Unicode range table is sorted and non-overlapping. */
        consteval bool range_table_is_sorted(std::span<const code_point_range> table) {
            for (std::size_t i = 0; i < table.size(); ++i) {
                if (table[i].lowest > table[i].highest) return false;
                if (i > 0 && table[i].lowest <= table[i - 1].highest) return false;
            }
            return !table.empty();
        }

        static_assert(range_table_is_sorted(east_asian_wide_ranges),
                      "clapp: the East Asian Width table must be sorted and disjoint, or "
                      "in_range() bisects into the wrong half and characters silently "
                      "measure one cell wide.");
        static_assert(range_table_is_sorted(zero_width_ranges),
                      "clapp: the zero-width table must be sorted and disjoint.");

    }  // namespace detail

    /**
     * \brief Terminal cells for \p code_point (0, 1, or 2).
     * \param code_point Unicode scalar or unpaired surrogate (WTF-8).
     * \return Cell count. Surrogates → 1 (matches ill-formed subpart accounting).
     */
    [[nodiscard]] constexpr std::size_t char_display_width(char32_t code_point) noexcept {
        const std::uint32_t value = static_cast<std::uint32_t>(code_point);
        // Controls first: they are the overwhelmingly common case in help text
        // (every '\n') and would otherwise pay for two bisections.
        if (value < 0x20u || (value >= 0x7Fu && value <= 0x9Fu)) return 0;
        if (value < 0x300u) return 1;  // below the first zero-width range
        if (detail::in_range(detail::zero_width_ranges, value)) return 0;
        if (detail::in_range(detail::east_asian_wide_ranges, value)) return 2;
        return 1;
    }

    namespace detail {

        /**
         * \brief Byte length of the ANSI escape starting at \p pos.
         * \param text Bytes to scan.
         * \param pos Offset of `ESC` (`0x1B`).
         * \pre `pos < text.size() && text[pos] == 0x1B`.
         * \return Bytes to skip, always >= 1. Truncated sequence consumes the rest;
         *         stray ESC consumes only itself.
         * \note ECMA-48: CSI (`ESC [`) ends at `0x40`–`0x7E` after params/intermediates;
         *       OSC/DCS/SOS/PM/APC until BEL or ST (`ESC` + backslash); nF then final;
         *       final-only → two bytes (`ESC 7`, `ESC =`).
         *
         * \warning **Byte after ESC decides if a sequence exists; often it does not.**
         *          Only `0x20`–`0x7E` are legal introducers. C0, DEL, or high-bit →
         *          return 1. A fixed two-byte consume (the old bug) cuts what follows:
         *          - (W)TF-8 lead eaten → orphan continuations / invalid UTF-8;
         *          - newline eaten → paragraphs merge;
         *          - second ESC eaten → body becomes visible text.
         *          Silent (no sanitizer); reachable from argv, `[env: VAR=…]`, builder
         *          strings. Byte-class endpoints pinned in textwrap_test.cpp
         *          (`escape_byte_classes`). Trap 16.
         */
        [[nodiscard]] constexpr std::size_t escape_sequence_length(std::string_view text,
                                                                   std::size_t pos) noexcept {
            const std::size_t size = text.size();
            if (pos + 1 >= size) return 1;
            const std::uint8_t introducer = byte_at(text, pos + 1);

            if (introducer == 0x5Bu) {  // '[' — CSI
                std::size_t i = pos + 2;
                while (i < size && byte_at(text, i) >= 0x30u && byte_at(text, i) <= 0x3Fu) ++i;
                while (i < size && byte_at(text, i) >= 0x20u && byte_at(text, i) <= 0x2Fu) ++i;
                if (i < size && byte_at(text, i) >= 0x40u && byte_at(text, i) <= 0x7Eu) ++i;
                return i - pos;
            }

            const bool string_sequence = introducer == 0x5Du      // ']' OSC
                                         || introducer == 0x50u   // 'P' DCS
                                         || introducer == 0x58u   // 'X' SOS
                                         || introducer == 0x5Eu   // '^' PM
                                         || introducer == 0x5Fu;  // '_' APC
            if (string_sequence) {
                std::size_t i = pos + 2;
                while (i < size) {
                    const std::uint8_t byte = byte_at(text, i);
                    if (byte == 0x07u) return i + 1 - pos;  // BEL
                    if (byte == 0x1Bu && i + 1 < size && byte_at(text, i + 1) == 0x5Cu)
                        return i + 2 - pos;  // ESC '\'
                    ++i;
                }
                return size - pos;
            }

            if (introducer >= 0x20u && introducer <= 0x2Fu) {  // nF: charset designators
                std::size_t i = pos + 2;
                while (i < size && byte_at(text, i) >= 0x20u && byte_at(text, i) <= 0x2Fu) ++i;
                if (i < size && byte_at(text, i) >= 0x30u && byte_at(text, i) <= 0x7Eu) ++i;
                return i - pos;
            }

            // A final byte on its own: the whole sequence is `ESC` plus this one byte.
            if (introducer >= 0x30u && introducer <= 0x7Eu) return 2;

            // Not an introducer of anything. See this function's \warning: the `ESC`
            // is stray and takes nothing with it.
            return 1;
        }

        /**
         * \brief First offset at/after \p pos not inside an escape (shared scanner).
         * \param text Bytes to scan.
         * \param pos Start; need not be ESC.
         * \return \p pos if not ESC; else past the escape run. In [\p pos, size].
         * \note Sole definition used by display_width and strip_escapes.
         */
        [[nodiscard]] constexpr std::size_t skip_escapes_at(std::string_view text,
                                                            std::size_t pos) noexcept {
            while (pos < text.size() && byte_at(text, pos) == 0x1Bu)
                pos += escape_sequence_length(text, pos);
            return pos < text.size() ? pos : text.size();
        }

        /** \brief One character of \p text with the escape sequences taken out. */
        struct clean_char {
            std::uint32_t code_point = 0;      /**< Meaningful only when #ok. */
            std::size_t next         = 0;      /**< Raw offset just past this character. */
            bool ok                  = true;   /**< Whether the bytes decoded. */
            bool present             = false;  /**< `false` ⇒ nothing left to decode. */
        };

        /**
         * \brief Decode the character at/after \p pos, ignoring escapes.
         * \param text Raw (W)TF-8, possibly with escapes.
         * \param pos Raw start offset.
         * \return Character, or present==false when only escapes remain.
         * \note Window is the escape-free projection so strip rejoins split UTF-8 halves
         *       into one character and display_width matches strip_escapes by construction.
         */
        [[nodiscard]] constexpr clean_char next_clean_char(std::string_view text,
                                                           std::size_t pos) noexcept {
            const std::size_t start = skip_escapes_at(text, pos);
            if (start >= text.size()) return {};

            // start is not an `ESC` and is in range, so the loop below runs at least
            // once and scan_one() is never handed an empty view.
            const std::size_t wanted = byte_at(text, start) < 0x80u ? 1u : 4u;
            std::array<char, 4> window{};
            std::array<std::size_t, 5> ends{};  // ends[k] = raw offset past window byte k
            std::size_t count = 0;
            std::size_t raw   = start;
            while (count < wanted) {
                raw = skip_escapes_at(text, raw);
                if (raw >= text.size()) break;
                window[count] = text[raw];
                ++raw;
                ends[++count] = raw;
            }

            const scan_result scan = scan_one(std::string_view{window.data(), count}, 0);
            // scan_one never reports more bytes than it was given, so `taken` is only a
            // guard against a future change to it — the clamp keeps this indexing in
            // range without a `\pre` no caller could check.
            const std::size_t taken = scan.length < count ? scan.length : count;
            return {.code_point = scan.code_point,
                    .next       = ends[taken],
                    .ok         = scan.ok,
                    .present    = true};
        }

    }  // namespace detail

    /**
     * \brief Terminal cells \p text occupies (not bytes, not code points).
     * \param text (W)TF-8; ill-formed sequences allowed.
     * \return Sum of char_display_width; escapes skipped; one cell per ill-formed subpart.
     * \note Measures what strip_escapes emits (shared next_clean_char scanner). Escape
     *       between halves of a character rejoins them (e.g. width 1 for `é`).
     */
    [[nodiscard]] constexpr std::size_t display_width(std::string_view text) noexcept {
        std::size_t width = 0;
        std::size_t pos   = 0;
        while (true) {
            const detail::clean_char step = detail::next_clean_char(text, pos);
            if (!step.present) return width;
            width += step.ok ? char_display_width(static_cast<char32_t>(step.code_point)) : 1;
            pos = step.next;
        }
    }

    /**
     * \brief \p text with every ANSI escape removed (ECMA-48 boundaries, not "to m").
     * \param text (W)TF-8, possibly with escapes.
     * \return Copy with no ESC/sequence bytes; lone controls (tab/newline) kept.
     * \note Shared scanner with display_width. Producers (help/usage/version/error)
     *       end with this so styled_str never holds ANSI. 差异清单 #29. Idempotent.
     */
    [[nodiscard]] constexpr std::string strip_escapes(std::string_view text) {
        std::string out;
        std::size_t pos = 0;
        while (true) {
            pos = detail::skip_escapes_at(text, pos);
            if (pos >= text.size()) return out;
            out.push_back(text[pos]);
            ++pos;
        }
    }

    /**
     * \brief strip_escapes per fragment; empty fragments dropped (merge invariant).
     * \param message Message to clean.
     * \return Copy with same classes; clean input preserves fragment identities.
     * \note Hand-written ESC scan, not string::find (trap 10 / ubsan consteval).
     */
    [[nodiscard]] constexpr styled_str strip_escapes(const styled_str& message) {
        bool has_escape = false;
        for (const styled_span& fragment : message.spans()) {
            for (const char byte : fragment.text) {
                if (byte == '\x1B') has_escape = true;
            }
        }
        if (!has_escape) return message;

        styled_str out;
        for (const styled_span& fragment : message.spans()) {
            const std::string cleaned = strip_escapes(std::string_view{fragment.text});
            out.push(fragment.class_, cleaned);
        }
        return out;
    }

    namespace detail {

        /**
         * \brief Six ASCII whitespace bytes (not the trim rule; see is_unicode_space).
         */
        [[nodiscard]] constexpr bool is_ascii_space(char byte) noexcept {
            return byte == ' ' || byte == '\t' || byte == '\n' || byte == '\r' || byte == '\v' ||
                   byte == '\f';
        }

        /**
         * \brief Unicode White_Space (Rust/clap `char::is_whitespace`; ten ranges).
         * \param code_point Scalar or unpaired surrogate.
         * \return true for those ranges only (not U+200B / U+180E).
         */
        [[nodiscard]] constexpr bool is_unicode_space(char32_t code_point) noexcept {
            const std::uint32_t value = static_cast<std::uint32_t>(code_point);
            if (value < 0x09u) return false;
            if (value <= 0x20u) return value <= 0x0Du || value == 0x20u;
            if (value < 0x85u) return false;
            return value == 0x85u || value == 0xA0u || value == 0x1680u ||
                   (value >= 0x2000u && value <= 0x200Au) || value == 0x2028u || value == 0x2029u ||
                   value == 0x202Fu || value == 0x205Fu || value == 0x3000u;
        }

        /**
         * \brief \p text with trailing Unicode whitespace removed (forward scan).
         * \param text (W)TF-8; ill-formed never counts as whitespace.
         * \return Prefix ending in a non-space character.
         */
        [[nodiscard]] constexpr std::string_view trim_space_end(std::string_view text) {
            std::size_t content = 0;
            std::size_t pos     = 0;
            while (pos < text.size()) {
                const scan_result scan = scan_one(text, pos);
                pos += scan.length;
                if (!scan.ok || !is_unicode_space(static_cast<char32_t>(scan.code_point)))
                    content = pos;
            }
            return text.substr(0, content);
        }

        /**
         * \brief One past the word starting at \p pos (non-`U+0020` run + trailing spaces).
         * \param line Bytes to scan.
         * \param pos First byte of the word.
         * \pre `pos < line.size()`.
         * \return Offset one past the word; always > \p pos.
         * \note Only `U+0020` separates (not tab/newline/NBSP). Caller already split on newline.
         *
         * \warning **ANSI escape is atomic — never break inside it.** `U+0020` is legal
         *          inside CSI intermediates (`ESC [ 1 SP q`) and OSC payloads, so a
         *          space-only split ends a line mid-sequence and the terminal swallows
         *          what follows. Steps via escape_sequence_length() (same unit as
         *          display_width). Trap 15: wrap of DECSCUSR once ended with bare `ESC [ 1`.
         */
        [[nodiscard]] constexpr std::size_t space_word_end(std::string_view line,
                                                           std::size_t pos) noexcept {
            std::size_t end = pos;
            if (line[pos] != ' ') {
                while (end < line.size() && line[end] != ' ') {
                    // escape_sequence_length() never returns 0, so this terminates.
                    end += byte_at(line, end) == 0x1Bu ? escape_sequence_length(line, end) : 1;
                }
            }
            while (end < line.size() && line[end] == ' ') ++end;
            return end;
        }

        /**
         * \brief One past last non-trailing-whitespace byte of \p word.
         * \param word As space_word_end delimits it.
         * \return Content end; 0 if all whitespace.
         * \note Space inside an escape is content — must not retract it at a break.
         */
        [[nodiscard]] constexpr std::size_t word_content_end(std::string_view word) noexcept {
            std::size_t content = 0;
            std::size_t pos     = 0;
            while (pos < word.size()) {
                if (byte_at(word, pos) == 0x1Bu) {
                    pos += escape_sequence_length(word, pos);
                    content = pos;
                    continue;
                }
                const scan_result scan = scan_one(word, pos);
                pos += scan.length;
                if (!scan.ok || !is_unicode_space(static_cast<char32_t>(scan.code_point)))
                    content = pos;
            }
            return content;
        }

        /** \brief Whether \p text is a non-empty run of `U+0020` and nothing else. */
        [[nodiscard]] constexpr bool is_space_run(std::string_view text) noexcept {
            if (text.empty()) return false;
            for (const char byte : text) {
                if (byte != ' ') return false;
            }
            return true;
        }

        /**
         * \brief \p count spaces (push_back; trap 10 avoids string(n,' ') in consteval).
         */
        [[nodiscard]] constexpr std::string spaces(std::size_t count) {
            std::string out;
            for (std::size_t i = 0; i < count; ++i) out.push_back(' ');
            return out;
        }

        // Sinks: word (input slice), inserted newline, inserted hanging indent.
        // Call site names the kind — never recover by comparing output vs input
        // pointers (trap 10 / ubsan consteval).

        /** \brief Emit \p word (input slice) into \p out. */
        constexpr void sink_word(std::string& out, std::string_view word) {
            append_bytes(out, word);
        }

        /** \brief Emit the `'\n'` that a line break inserts. */
        constexpr void sink_newline(std::string& out) { out.push_back('\n'); }

        /** \brief Emit the hanging indent that a line break inserts. */
        constexpr void sink_indent(std::string& out, std::string_view indent) {
            append_bytes(out, indent);
        }

        /** \brief A mark that sink_retract() can later wind \p out back to. */
        [[nodiscard]] constexpr std::size_t sink_mark(const std::string& out) noexcept {
            return out.size();
        }

        /** \brief Undo everything emitted into \p out after \p mark. */
        constexpr void sink_retract(std::string& out, std::size_t mark) {
            while (out.size() > mark) out.pop_back();
        }

        /**
         * \brief Sink that tags every emitted byte with a style_class.
         * \note #consumed maps input order → class; no pointer arithmetic (trap 10).
         */
        struct classed_sink {
            std::string text{};                  /**< Bytes emitted so far. */
            std::vector<style_class> classes{};  /**< `classes[i]` belongs to `text[i]`. */
            std::vector<style_class> source{};   /**< Class of each input byte. */
            std::size_t consumed = 0;            /**< Input bytes emitted so far. */

            /**
             * \brief Class of the next input byte (word about to be emitted).
             * \return plain if empty; last class once exhausted (breaks precede words).
             */
            [[nodiscard]] constexpr style_class next_class() const noexcept {
                if (consumed < source.size()) return source[consumed];
                if (source.empty()) return style_class::plain;
                return source[source.size() - 1];
            }
        };

        /** \brief Emit \p word, tagging each byte with the class it arrived with. */
        constexpr void sink_word(classed_sink& out, std::string_view word) {
            for (const char byte : word) {
                out.text.push_back(byte);
                out.classes.push_back(out.next_class());
                ++out.consumed;
            }
        }

        /**
         * \brief Emit inserted newline, tagged with the class of the word it precedes.
         * \note Class of the *following* word (clap); to_string is blind to the mistake.
         */
        constexpr void sink_newline(classed_sink& out) {
            out.text.push_back('\n');
            out.classes.push_back(out.next_class());
        }

        /** \brief Emit an inserted indent, tagged like the `'\n'` that preceded it. */
        constexpr void sink_indent(classed_sink& out, std::string_view indent) {
            const style_class cls = out.next_class();
            for (const char byte : indent) {
                out.text.push_back(byte);
                out.classes.push_back(cls);
            }
        }

        /** \brief A mark that sink_retract() can later wind \p out back to. */
        [[nodiscard]] constexpr std::size_t sink_mark(const classed_sink& out) noexcept {
            return out.text.size();
        }

        /**
         * \brief Undo emissions after \p mark (does not rewind `consumed`).
         */
        constexpr void sink_retract(classed_sink& out, std::size_t mark) {
            while (out.text.size() > mark) {
                out.text.pop_back();
                out.classes.pop_back();
            }
        }

    }  // namespace detail

    /** \brief Width that disables wrapping (clap usize::MAX / term_width(0)). */
    inline constexpr std::size_t unbounded_width = std::numeric_limits<std::size_t>::max();

    /** \brief Fallback width when no TTY (clap: 100, not 80). */
    inline constexpr std::size_t default_terminal_width = 100;

    /**
     * \brief Incremental greedy line breaker (never splits a word).
     *
     * Column and hanging indent survive across wrap_into calls; retraction point does
     * not (clap `0 < i`). Over-wide words get their own overflowing line.
     *
     * \code
     *     clapp::line_wrapper wrapper{20};
     *     wrapper.wrap_into("12345 12345 ", out);
     *     wrapper.wrap_into("12345 12345", out);  // "...12345" then newline then "12345"
     * \endcode
     *
     * \note #reset() starts a new logical line; embedded newline does the same.
     */
    class line_wrapper {
    public:
        /**
         * \brief A wrapper that breaks lines wider than \p hard_width cells.
         * \param hard_width Cells per line; clapp::unbounded_width disables breaking.
         */
        constexpr explicit line_wrapper(std::size_t hard_width) noexcept
            : hard_width_(hard_width) {}

        /** \brief Start a new logical line: forget the column count and the indent. */
        constexpr void reset() noexcept {
            line_width_ = 0;
            indentation_.clear();
            indentation_known_ = false;
        }

        /** \brief The width this wrapper breaks at. */
        [[nodiscard]] constexpr std::size_t hard_width() const noexcept { return hard_width_; }

        /** \brief Cells already emitted on the line currently being built. */
        [[nodiscard]] constexpr std::size_t line_width() const noexcept { return line_width_; }

        /**
         * \brief Hanging indent from the current line's leading spaces.
         * \return Empty before text or when the line did not start with spaces.
         *
         * \warning View borrows `*this`; invalidated by wrap_into / reset (trap 12).
         */
        [[nodiscard]] constexpr std::string_view indentation() const noexcept {
            return indentation_;
        }

        /**
         * \brief Wrap \p text and append to \p out.
         * \param text Bytes; embedded newline ends a line (not a word separator).
         * \param out Destination (appended).
         * \note Leading spaces become hanging indent.
         *
         * \warning Trailing whitespace dropped **at a break**, kept **at end of input**
         *          (e.g. final spaces of the input survive). Callers or styled wrap trim
         *          afterwards.
         *
         * \tparam Sink string or classed_sink; five named ops (word/newline/indent/
         *         mark/retract) — no pointer compares (trap 10).
         */
        template<class Sink>
        constexpr void wrap_into(std::string_view text, Sink& out) {
            std::size_t pos = 0;
            while (pos < text.size()) {
                const std::size_t newline = text.find('\n', pos);
                const bool ends_line      = newline != std::string_view::npos;
                const std::size_t end     = ends_line ? newline + 1 : text.size();
                wrap_one_line(text.substr(pos, end - pos), out);
                if (ends_line) reset();
                pos = end;
            }
        }

    private:
        /**
         * \brief Whether \p current + \p added would overflow (safe with unbounded_width).
         */
        [[nodiscard]] constexpr bool exceeds(std::size_t current,
                                             std::size_t added) const noexcept {
            return current > hard_width_ || hard_width_ - current < added;
        }

        /** \brief Wrap one line — text with at most one `'\n'`, at the very end. */
        template<class Sink>
        constexpr void wrap_one_line(std::string_view line, Sink& out) {
            bool first_word = false;
            if (!indentation_known_ && !line.empty()) {
                // The first word of a logical line is never wrapped, even when it is
                // wider than the whole terminal: there is nowhere better to put it.
                first_word                  = true;
                indentation_known_          = true;
                const std::string_view head = line.substr(0, detail::space_word_end(line, 0));
                if (detail::is_space_run(head)) detail::append_bytes(indentation_, head);
            }

            // Where the previous word's trailing spaces begin, so that a break can
            // retract them. Scoped to this line, exactly like clap's `0 < i` guard.
            std::size_t trim_point = detail::sink_mark(out);
            bool can_trim          = false;

            std::size_t pos = 0;
            while (pos < line.size()) {
                const std::size_t end       = detail::space_word_end(line, pos);
                const std::string_view word = line.substr(pos, end - pos);
                // Not trim_space_end(): a space inside an escape sequence is content.
                const std::string_view trimmed = word.substr(0, detail::word_content_end(word));
                const std::size_t body         = display_width(trimmed);
                const std::size_t tail         = display_width(word.substr(trimmed.size()));

                if (first_word && body > 0) {
                    first_word = false;
                } else if (exceeds(line_width_, body)) {
                    if (can_trim) detail::sink_retract(out, trim_point);
                    detail::sink_newline(out);
                    line_width_ = 0;
                    detail::sink_indent(out, indentation_);
                    line_width_ += display_width(indentation_);
                }

                detail::sink_word(out, word);
                trim_point = detail::sink_mark(out) - (word.size() - trimmed.size());
                can_trim   = true;
                line_width_ += body + tail;
                pos = end;
            }
        }

        std::size_t hard_width_ = 0;
        std::size_t line_width_ = 0;
        std::string indentation_{};
        bool indentation_known_ = false;
    };

    /**
     * \brief Break \p content into lines ≤ \p hard_width cells.
     * \param content Text; existing newlines survive.
     * \param hard_width Cells per line; unbounded_width leaves content unchanged.
     * \return Wrapped text (bytes preserved aside from retracted break spaces).
     */
    [[nodiscard]] constexpr std::string wrap(std::string_view content, std::size_t hard_width) {
        std::string out;
        line_wrapper wrapper{hard_width};
        wrapper.wrap_into(content, out);
        return out;
    }

    /**
     * \brief \p text with trailing Unicode White_Space removed (clap/Rust trim_end).
     * \param text Text to trim.
     * \return Copy without trailing whitespace.
     */
    [[nodiscard]] constexpr std::string trim_end(std::string_view text) {
        std::string out;
        detail::append_bytes(out, detail::trim_space_end(text));
        return out;
    }

    /**
     * \brief \p message with trailing whitespace removed; empty fragments dropped.
     * \param message Message to trim.
     * \return Copy preserving merge invariant.
     */
    [[nodiscard]] constexpr styled_str trim_end(const styled_str& message) {
        std::vector<styled_span> fragments(message.spans().begin(), message.spans().end());
        while (!fragments.empty()) {
            std::string& tail      = fragments.back().text;
            const std::size_t keep = detail::trim_space_end(std::string_view{tail}).size();
            while (tail.size() > keep) tail.pop_back();
            if (!tail.empty()) break;
            fragments.pop_back();
        }
        styled_str out;
        for (const styled_span& fragment : fragments) out.push(fragment.class_, fragment.text);
        return out;
    }

    /**
     * \brief Wrap \p message to \p hard_width; preserve classes; trim_end.
     * \param message Semantic fragments.
     * \param hard_width Cells per line; unbounded_width disables.
     * \param palette Effective style per class (non-empty style = run boundary).
     * \return Wrapped message; inserted newline/indent take the following word's class.
     * \note One line_wrapper across runs; empty-style neighbours coalesce first.
     */
    [[nodiscard]] constexpr styled_str
    wrap(const styled_str& message, std::size_t hard_width, const styles& palette) {
        detail::classed_sink sink;
        for (const styled_span& fragment : message.spans()) {
            for (std::size_t i = 0; i < fragment.text.size(); ++i)
                sink.source.push_back(fragment.class_);
        }

        line_wrapper wrapper{hard_width};
        std::string plain_run;
        const auto flush_plain = [&] {
            if (plain_run.empty()) return;
            wrapper.wrap_into(plain_run, sink);
            plain_run.clear();
        };

        for (const styled_span& fragment : message.spans()) {
            if (palette.get(fragment.class_) == style{}) {
                detail::append_bytes(plain_run, fragment.text);
            } else {
                flush_plain();
                wrapper.wrap_into(fragment.text, sink);
            }
        }
        flush_plain();

        // Back into fragments: one run per stretch of bytes sharing a class. push()
        // merges adjacent runs of one class and drops empty ones, so the result
        // satisfies clapp::styled_str's invariant whatever the input looked like.
        styled_str out;
        const std::string_view text = sink.text;
        std::size_t start           = 0;
        while (start < text.size()) {
            std::size_t end = start;
            while (end < text.size() && sink.classes[end] == sink.classes[start]) ++end;
            out.push(sink.classes[start], text.substr(start, end - start));
            start = end;
        }
        return trim_end(out);
    }

    /**
     * \brief Wrap \p message as one unstyled text run.
     * \param message Text to wrap, in semantic fragments.
     * \param hard_width Cells per line; clapp::unbounded_width disables breaking.
     * \return `wrap(message, hard_width, styles::plain())`.
     */
    [[nodiscard]] constexpr styled_str wrap(const styled_str& message, std::size_t hard_width) {
        return wrap(message, hard_width, styles::plain());
    }

    /**
     * \brief Prefix first line with \p initial, later lines with \p trailing.
     * \param text Text to indent.
     * \param initial Before first line (often empty).
     * \param trailing After every newline.
     * \return Indented text. Wrap first, then indent (not the reverse).
     */
    [[nodiscard]] constexpr std::string
    indent(std::string_view text, std::string_view initial, std::string_view trailing) {
        std::string out;
        detail::append_bytes(out, initial);
        for (const char byte : text) {
            out.push_back(byte);
            if (byte == '\n') detail::append_bytes(out, trailing);
        }
        return out;
    }

    /**
     * \brief Indent styled message; inserted runs are style_class::plain.
     * \param message Message to indent.
     * \param initial Before first line.
     * \param trailing After every newline.
     * \return Indented message.
     */
    [[nodiscard]] constexpr styled_str
    indent(const styled_str& message, std::string_view initial, std::string_view trailing) {
        styled_str out;
        out.push_plain(initial);
        for (const styled_span& fragment : message.spans()) {
            const std::string_view text = fragment.text;
            std::size_t start           = 0;
            for (std::size_t i = 0; i < text.size(); ++i) {
                if (text[i] != '\n') continue;
                out.push(fragment.class_, text.substr(start, i + 1 - start));
                out.push_plain(trailing);
                start = i + 1;
            }
            out.push(fragment.class_, text.substr(start));
        }
        return out;
    }

    /**
     * \brief Wrap then hang-indent (option description under the option name).
     * \param text Text to wrap.
     * \param total_width Full terminal width.
     * \param hanging_indent Cells for continuation lines (first line has no indent).
     * \return Wrapped, indented text. hanging_indent >= total_width → available 0.
     */
    [[nodiscard]] constexpr std::string
    wrap_hanging(std::string_view text, std::size_t total_width, std::size_t hanging_indent) {
        const std::size_t available =
                total_width > hanging_indent ? total_width - hanging_indent : 0;
        return indent(wrap(text, available), "", detail::spaces(hanging_indent));
    }

    /**
     * \brief clapp::wrap_hanging for a styled message.
     * \param message        The message to wrap.
     * \param total_width    The full terminal width in cells.
     * \param hanging_indent Cells the continuation lines are indented by.
     * \return The wrapped, indented message.
     */
    [[nodiscard]] constexpr styled_str
    wrap_hanging(const styled_str& message, std::size_t total_width, std::size_t hanging_indent) {
        const std::size_t available =
                total_width > hanging_indent ? total_width - hanging_indent : 0;
        return indent(wrap(message, available), "", detail::spaces(hanging_indent));
    }

    /**
     * \brief Parse a terminal width from an env value (e.g. COLUMNS).
     * \param text Value; optional leading `+`, bare decimal only.
     * \return Width, or nullopt if empty/zero/non-decimal/overflow.
     */
    [[nodiscard]] constexpr std::optional<std::size_t>
    parse_terminal_width(std::string_view text) noexcept {
        std::size_t pos = 0;
        if (pos < text.size() && text[pos] == '+') ++pos;
        if (pos >= text.size()) return std::nullopt;
        std::size_t value = 0;
        for (; pos < text.size(); ++pos) {
            const char byte = text[pos];
            if (byte < '0' || byte > '9') return std::nullopt;
            const std::size_t digit = static_cast<std::size_t>(byte - '0');
            if (value > (unbounded_width - digit) / 10) return std::nullopt;
            value = value * 10 + digit;
        }
        return value == 0 ? std::nullopt : std::optional<std::size_t>{value};
    }

    /**
     * \brief Width from `COLUMNS`, if set and usable.
     * \return Parsed width, or nullopt.
     *
     * \warning getenv races with setenv; pointer valid only until env changes.
     *          This copies before returning (once per help screen).
     */
    [[nodiscard]] inline std::optional<std::size_t> terminal_width_from_env() {
        const char* const columns = std::getenv("COLUMNS");
        if (columns == nullptr) return std::nullopt;
        return parse_terminal_width(std::string_view{columns});
    }

    /**
     * \brief Detect terminal width (stdout ioctl, then COLUMNS).
     * \return Width in cells, or nullopt when no TTY / pipe.
     * \note Pure policy is resolve_wrap_width; this is the only I/O here.
     *
     * \warning Windows: COLUMNS only (avoids pulling windows.h for console API).
     *          Use command_builder::term_width for an explicit width.
     */
    [[nodiscard]] inline std::optional<std::size_t> detect_terminal_width() {
#if !defined(_WIN32)
        ::winsize window{};
        if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &window) == 0 && window.ws_col > 0)
            return std::optional<std::size_t>{static_cast<std::size_t>(window.ws_col)};
#endif
        return terminal_width_from_env();
    }

    /**
     * \brief Width help text is wrapped at (clap HelpTemplate::term_w).
     *
     * Order: explicit term_width (0 → unbounded) wins; else detected or default
     * 100; then cap by max_term_width (unset/0 = no cap). Cap does not apply to
     * an explicit term_width.
     *
     * \param term_width command_spec::get_term_width().
     * \param max_term_width command_spec::get_max_term_width().
     * \param detected_width detect_terminal_width().
     * \return Width for wrap().
     *
     * \warning Builder maps term_width(0) → nullopt ("detect"), so "never wrap" is
     *          not expressible via the builder; this function still handles 0 when
     *          called with hand-built values.
     */
    [[nodiscard]] constexpr std::size_t
    resolve_wrap_width(std::optional<std::size_t> term_width,
                       std::optional<std::size_t> max_term_width,
                       std::optional<std::size_t> detected_width) noexcept {
        if (term_width.has_value())
            return *term_width == 0 ? unbounded_width : *term_width;

        const std::size_t current =
                detected_width.has_value() ? *detected_width : default_terminal_width;
        const std::size_t cap = (!max_term_width.has_value() || *max_term_width == 0)
                                        ? unbounded_width
                                        : *max_term_width;
        return current < cap ? current : cap;
    }

    namespace detail {

        /** \brief Verify terminal-cell widths for representative Unicode input. */
        consteval bool display_width_counts_cells_not_bytes() {
            if constexpr (display_width("Caf\xC3\xA9 Plain") != 10) return false;
            if constexpr (display_width("\xE4\xB8\xAD\xE6\x96\x87") != 4) return false;
            if constexpr (display_width("\xEF\xBD\x86\xEF\xBD\x95\xEF\xBD\x8C\xEF\xBD\x8C") != 8)
                return false;
            if constexpr (display_width("e\xCC\x81") != 1) return false;
            if constexpr (display_width("\xE2\x81\x89") != 1) return false;
            return display_width("\xE2\x81\x89\xEF\xB8\x8F") == 1;
        }

        static_assert(display_width_counts_cells_not_bytes(),
                      "clapp: display_width must count terminal cells. If this fails after a "
                      "table regeneration, the Unicode data changed shape, not clapp.");

        /** \brief Verify that wrapping preserves an over-wide word as one unit. */
        consteval bool wrap_never_drops_or_splits_a_long_word() {
            // A word wider than the line gets the line to itself and overflows it.
            if (wrap("foo", 0) != std::string_view{"foo"}) return false;
            if (wrap("foo bar", 0) != std::string_view{"foo\nbar"}) return false;
            return wrap("\xE4\xB8\xAD \xE6\x96\x87", 1) ==
                   std::string_view{"\xE4\xB8\xAD\n\xE6\x96\x87"};
        }

        static_assert(wrap_never_drops_or_splits_a_long_word());

        /** \brief Verify that wrapping preserves explicit line boundaries. */
        consteval bool wrap_honours_embedded_newlines() {
            if (wrap("ab\ncd", 10) != std::string_view{"ab\ncd"}) return false;
            return wrap("a b\nc d", 3) == std::string_view{"a b\nc d"};
        }

        static_assert(wrap_honours_embedded_newlines());

        /**
         * \brief Whether \p text has a truncated/stray ESC (terminal-unrecoverable shape).
         */
        consteval bool has_unterminated_escape(std::string_view text) {
            for (std::size_t pos = 0; pos < text.size();) {
                if (byte_at(text, pos) != 0x1Bu) {
                    ++pos;
                    continue;
                }
                const std::size_t length        = escape_sequence_length(text, pos);
                const std::string_view sequence = text.substr(pos, length);
                // A CSI or nF sequence is complete only when it ends in a final byte; a
                // string sequence only when it ends in BEL or ESC '\'.
                if (sequence.size() < 2) return true;
                const std::uint8_t last       = byte_at(sequence, sequence.size() - 1);
                const std::uint8_t introducer = byte_at(sequence, 1);
                const bool string_sequence    = introducer == 0x5Du || introducer == 0x50u ||
                                                introducer == 0x58u || introducer == 0x5Eu ||
                                                introducer == 0x5Fu;
                if (string_sequence) {
                    if (last != 0x07u && !(sequence.size() >= 2 && last == 0x5Cu)) return true;
                } else if (introducer == 0x5Bu) {
                    if (!(last >= 0x40u && last <= 0x7Eu)) return true;
                }
                pos += length;
            }
            return false;
        }

        static_assert(has_unterminated_escape("\x1B[1"), "the probe must detect its own case");
        static_assert(!has_unterminated_escape("\x1B[1 q"));

        /** \brief wrap never splits an escape (DECSCUSR SP / OSC spaces / SGR). */
        consteval bool wrap_never_splits_an_escape_sequence() {
            // DECSCUSR: ESC [ 1 SP q. The SP is a CSI intermediate byte.
            const std::string decscusr = wrap("aaaa \x1B[1 q bbbb", 6);
            // OSC window title, whose payload is prose and therefore full of spaces.
            const std::string osc = wrap("\x1B]0;My Long Title\x07 aaaa bbbb", 8);
            // The common case, which never had the bug: SGR carries no space.
            const std::string sgr = wrap("\x1B[31mred\x1B[0m and more words", 6);
            if (has_unterminated_escape(decscusr) || has_unterminated_escape(osc) ||
                has_unterminated_escape(sgr))
                return false;
            // The sequence is a zero-width word, so the break lands after it and the
            // space before it survives.
            if (strip_escapes(decscusr) != std::string_view{"aaaa \nbbbb"}) return false;
            return strip_escapes(sgr) == std::string_view{"red\nand\nmore\nwords"};
        }

        static_assert(wrap_never_splits_an_escape_sequence(),
                      "clapp: a line break must never land inside an ANSI escape sequence — "
                      "an unterminated sequence makes the terminal swallow what follows.");

        /** \brief strip_escapes removes whole sequences; identity on clean; idempotent. */
        consteval bool strip_escapes_removes_whole_sequences() {
            if (strip_escapes("\x1B[1m--help\x1B[0m x") != std::string_view{"--help x"})
                return false;
            if (strip_escapes("a\x1B[1 qb") != std::string_view{"ab"}) return false;
            if (strip_escapes("\x1B]0;title\x07z") != std::string_view{"z"}) return false;
            if (strip_escapes("\x1B(Bplain") != std::string_view{"plain"}) return false;
            if (strip_escapes("a\tb") != std::string_view{"a\tb"}) return false;
            if (strip_escapes("Usage: demo") != std::string_view{"Usage: demo"})
                return false;
            return strip_escapes(strip_escapes("\x1B[1mx\x1B[0m")) ==
                   strip_escapes("\x1B[1mx\x1B[0m");
        }

        static_assert(strip_escapes_removes_whole_sequences());

        /**
         * \brief Stray ESC consumes only itself (WTF-8 lead, C0, DEL, second ESC).
         */
        consteval bool a_stray_escape_takes_nothing_with_it() {
            if (escape_sequence_length("\x1B\xE4\xB8\xAD", 0) != 1) return false;
                   // A (W)TF-8 lead byte is not an introducer: the character survives whole
                   // and the result is still valid UTF-8, which `[B8 AD]` was not.
            if (strip_escapes("\x1B\xE4\xB8\xAD") != std::string_view{"\xE4\xB8\xAD"})
                return false;
                   // A C0 control is not an introducer: the paragraph break survives.
            if (strip_escapes("a\x1B\nb") != std::string_view{"a\nb"}) return false;
            if (strip_escapes("a\x1B\tb") != std::string_view{"a\tb"}) return false;
                   // DEL is not an introducer either — 0x7E is the last final byte.
            if (strip_escapes("\x1B\x7F" "x") != std::string_view{"\x7F" "x"})
                return false;
                   // An `ESC` is not an introducer, so the second one still introduces its
                   // own sequence rather than arriving as visible `[m`.
            if (strip_escapes("\x1B\x1B[mx") != std::string_view{"x"}) return false;
                   // The two-byte escapes that *are* real still go whole.
            if (strip_escapes("\x1B" "7x") != std::string_view{"x"}) return false;
            return strip_escapes("\x1B=x") == std::string_view{"x"};
        }

        static_assert(a_stray_escape_takes_nothing_with_it(),
                      "clapp: an ESC that introduces no sequence must consume only itself — "
                      "consuming a fixed two bytes cuts a UTF-8 character, a newline or a "
                      "second ESC in half.");

        /**
         * \brief display_width(text) == display_width(strip_escapes(text)) on hard cases.
         */
        consteval bool width_measures_what_strip_escapes_emits() {
            constexpr std::string_view cases[] = {
                    "\x1B[1m--help\x1B[0m",   // the common case, always agreed
                    "\xC3\x1B[m\xA9",         // removal rejoins the two halves into `é`
                    "\xE4\xB8\x1B[m\xAD",     // and again, three bytes deep
                    "\x1B\xE4\xB8\xAD",       // stray ESC before a wide character
                    "a\x1B\nb",               // stray ESC before a newline
                    "\x1B]0;My Title\x07 x",  // OSC payload full of spaces
                    "a\x1B[1 qb",             // CSI with an intermediate byte
                    "\xFF\x1B[m\xFF",         // ill-formed on both sides, no rejoin
            };
            for (const std::string_view text : cases) {
                if (display_width(text) != display_width(std::string_view{strip_escapes(text)}))
                    return false;
            }
            if constexpr (display_width("\xC3\x1B[m\xA9") != 1) return false;
            return display_width("\x1B\xE4\xB8\xAD") == 2;
        }

        static_assert(width_measures_what_strip_escapes_emits(),
                      "clapp: display_width() must measure the text strip_escapes() emits — "
                      "the renderer measures first and strips last, so a disagreement lands "
                      "the description column a cell off.");

        /** \brief strip_escapes preserves classes; identity when clean. */
        consteval bool strip_escapes_preserves_classes() {
            styled_str dirty;
            dirty.push(style_class::header, "Options:")
                    .push_plain("\n  ")
                    .push(style_class::literal, "--he\x1B[1mlp");
            const styled_str clean = strip_escapes(dirty);

            styled_str expected;
            expected.push(style_class::header, "Options:")
                    .push_plain("\n  ")
                    .push(style_class::literal, "--help");

            styled_str untouched;
            untouched.push(style_class::header, "Usage:").push_plain(" demo");
            return clean == expected && strip_escapes(untouched) == untouched;
        }

        static_assert(strip_escapes_preserves_classes());

    }  // namespace detail

}  // namespace clapp
