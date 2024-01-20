/*
This bug-demo project is a very simplified version of the original issue.
For demonstation, remove/add MSVC_WORKAROUND_FOR_THIS_ERROR define.

My workaround was to put some variables as volatile.

If not, then bad code is generated for WidthInBytes >= 8.

OS and Compiler info: 
Win11 Pro, MSVC 17.8.4, x64 release, optimize for speed, v143 toolset)

(Its primary purpose was to make an outlined version of a character bitmap)

Personal tip: SIMD instructions branch which kicks in over 8 bytes + src_curr/src_prev usage.

How to check:
  bitmap_outline[1][0] should be 0x88 and not 0xf8.

bitmap_in   bitmap_outline   bitmap_outline
            (good)           (bad codegen)
00000000    11111000         11111000
01110000 -> 10001000         11111000
00000000    11111000         11111000
            F8,88,F8         F8,F8,F8
*/

#include <iostream>
#include <vector>

// removing the define demonstrates the compiler bug
//#define MSVC_WORKAROUND_FOR_THIS_ERROR

void bad_codegen_test(int width_in_bytes) {

#ifdef MSVC_WORKAROUND_FOR_THIS_ERROR
  volatile uint8_t* src_prev;
  volatile uint8_t* src_curr;
#else
  uint8_t* src_prev;
  uint8_t* src_curr;
#endif

  int height = 3; // 3 pixels (height)

  // make 2d test arrays, WidthInBytes bytes (8*WidthInBytes pixels) width
  std::vector<std::vector<uint8_t>> bitmap_in(height, std::vector<uint8_t>(width_in_bytes, 0));
  // Create test bitmap input
  bitmap_in[1][0] = 0x70; // 01110000

  // output with the same size. Later we'll check its line#1 for the good/bad result
  std::vector<std::vector<uint8_t>> bitmap_outline(height, std::vector<uint8_t>(width_in_bytes, 0));

  auto h = bitmap_in.size();
  auto w = bitmap_in[0].size();

  // circular line buffer for holding precalculated shifted lines
  std::vector<uint8_t> buf1(w);
  std::vector<uint8_t> buf2(w);
  std::vector<uint8_t> buf3(w);

  // shift a line left and rights and result is or'd
  auto combine_shift_left_right = [](uint8_t* dst, auto src, size_t w) {
    if (w == 1)
    {
      *dst = (src[0] << 1) | (src[0] >> 1);
      return;
    }
    // leftmost
    uint8_t left = (src[0] << 1) | (src[1] >> (8 - 1));
    uint8_t right = 0 | (src[0] >> 1);
    *dst = left | right;
    dst++;
    src++;
    // middle
    for (size_t i = 1; i < w - 1; ++i)
    {
      left = (src[0] << 1) | (src[1] >> (8 - 1));
      right = (src[-1] << (8 - 1)) | (src[0] >> 1);
      *dst++ = left | right;
      src++;
    }
    // rightmost
    left = (src[0] << 1) | 0;
    right = (src[-1] << (8 - 1)) | (src[0] >> 1);
    *dst = left | right;
    };

  uint8_t* src_next;
  uint8_t* dst;

  uint8_t* tmp_line;
  uint8_t* prev_line_LR = buf1.data();
  uint8_t* curr_line_LR = buf2.data();
  uint8_t* next_line_LR = buf3.data();

  // line 0, no previous line
  size_t y = 0;
  dst = bitmap_outline[y].data();
  src_curr = bitmap_in[y].data();
  src_next = bitmap_in[y + 1].data();
  combine_shift_left_right(curr_line_LR, src_curr, w);
  combine_shift_left_right(next_line_LR, src_next, w);
  for (size_t x = 0; x < w; x++) {
    dst[x] = (curr_line_LR[x] | next_line_LR[x] | src_next[x]) & ~src_curr[x];
  }
  // re-use and cycle buffers
  tmp_line = prev_line_LR;
  prev_line_LR = curr_line_LR;
  curr_line_LR = next_line_LR;
  next_line_LR = tmp_line;

  src_prev = src_curr;
  src_curr = src_next;
  y++;

  // middle lines, y runs on 1..(h-2)
  for (; y < h - 1; y++)
  {
    dst = bitmap_outline[y].data();
    src_next = bitmap_in[y + 1].data();
    combine_shift_left_right(next_line_LR, src_next, w);
    for (size_t x = 0; x < w; x++) {
      dst[x] = (prev_line_LR[x] | curr_line_LR[x] | next_line_LR[x] | src_prev[x] | src_next[x]) & ~src_curr[x];
    }
    // re-use and cycle buffers
    tmp_line = prev_line_LR;
    prev_line_LR = curr_line_LR;
    curr_line_LR = next_line_LR;
    next_line_LR = tmp_line;
    // shift variables prev <- current <- next
    src_prev = src_curr;
    src_curr = src_next;
  }
  // last one, no next line
  dst = bitmap_outline[y].data();
  for (size_t x = 0; x < w; x++) {
    dst[x] = (prev_line_LR[x] | curr_line_LR[x] | src_prev[x]) & ~src_curr[x];
  }

  std::cout << "Byte width = " << width_in_bytes << ", ";

#ifdef MSVC_WORKAROUND_FOR_THIS_ERROR
  std::cout << " With workaround, ";
#else
  std::cout << " No workaround, ";
#endif

  std::cout << " Result is ";
  std::cout << (bitmap_outline[1][0] != 0x88 ? "bad" : "good");
  std::cout << std::endl;
}

/*
#define MSVC_WORKAROUND_FOR_THIS_ERROR
Byte width = 1,  With workaround,  Result is good
Byte width = 7,  With workaround,  Result is good
Byte width = 8,  With workaround,  Result is good
Byte width = 9,  With workaround,  Result is good

//#define MSVC_WORKAROUND_FOR_THIS_ERROR
Byte width = 1,  No workaround,  Result is good
Byte width = 7,  No workaround,  Result is good
Byte width = 8,  No workaround,  Result is bad
Byte width = 9,  No workaround,  Result is bad
*/
int main()
{
  bad_codegen_test(1);
  bad_codegen_test(7);
  bad_codegen_test(8); // wrong if release + no workaround
  bad_codegen_test(9); // wrong if release + no workaround
}
