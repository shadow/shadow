#include "vdl-list.h"
#include "internal-test.h"
#include <vector>
#include <algorithm>
#include <stdarg.h>


#define CHECK_LIST(list,expected_size,...)				\
  {									\
    std::vector<int> expected = get_expected (expected_size, ##__VA_ARGS__); \
    std::vector<int> got = get_list (list);				\
    if (!check (expected, got, __FILE__, __LINE__))			\
      {									\
	return false;							\
      }									\
  }

static std::vector<int> get_expected (uint32_t n, ...)
{
  std::vector<int> expected;
  va_list ap;
  va_start (ap, n);
  for (uint32_t i = 0; i < n; i++)
    {
      int value = va_arg (ap, int);
      expected.push_back (value);
    }
  va_end (ap);
  return expected;
}
static std::vector<int> get_list (struct VdlList *list)
{
  std::vector<int> vector;
  void **i;
  for (i = vdl_list_begin (list);
       i != vdl_list_end (list);
       i = vdl_list_next (i))
    {
      vector.push_back ((int)(long)(*i));
    }
  std::vector<int> inverted;
  for (i = vdl_list_rbegin (list);
       i != vdl_list_rend (list);
       i = vdl_list_rnext (i))
    {
      inverted.push_back ((int)(long)(*i));
    }
  std::reverse (inverted.begin (), inverted.end ());
  if (inverted != vector)
    {
      // make sure the caller fails.
      return std::vector<int> (vector.size () + 1);
    }
  return vector;
}
static bool check (std::vector<int> expected,
		   std::vector<int> got,
		   const char *file, int line)
{
  INTERNAL_TEST_ASSERT_EQ_VERBOSE (expected.size (), got.size (),
				   file,line);
  uint32_t n = std::min (expected.size (), got.size ());
  for (uint32_t i = 0; i < n; i++)
    {
      INTERNAL_TEST_ASSERT_EQ_VERBOSE (expected[i], got[i], file, line);
    }
  return true;
}

static bool cmp_int (void *a, void *b, void *ctx)
{
  int ia = (int)(long)a;
  int ib = (int)(long)b;
  return ia < ib;
}

bool test_list (void)
{
  VdlList *list = vdl_list_new ();
  vdl_list_delete (list);
  list = vdl_list_new ();
  INTERNAL_TEST_ASSERT (vdl_list_empty (list));
  vdl_list_push_back (list, (void*)1);
  INTERNAL_TEST_ASSERT (!vdl_list_empty (list));
  INTERNAL_TEST_ASSERT_EQ (vdl_list_size (list), 1);
  vdl_list_push_back (list, (void*)5);
  INTERNAL_TEST_ASSERT (!vdl_list_empty (list));
  INTERNAL_TEST_ASSERT_EQ (vdl_list_size (list), 2);
  INTERNAL_TEST_ASSERT_EQ (vdl_list_front (list), (void*)1);
  INTERNAL_TEST_ASSERT_EQ (vdl_list_back (list), (void*)5);
  CHECK_LIST (list, 2, 1, 5);
  vdl_list_reverse (list);
  CHECK_LIST (list, 2, 5, 1);
  vdl_list_reverse (list);
  CHECK_LIST (list, 2, 1, 5);
  vdl_list_reverse (list);
  CHECK_LIST (list, 2, 5, 1);
  vdl_list_reverse (list);
  CHECK_LIST (list, 2, 1, 5);
  vdl_list_pop_back (list);
  CHECK_LIST (list, 1, 1);
  vdl_list_pop_front (list);
  CHECK_LIST (list, 0);
  vdl_list_push_front (list, (void*)7);
  vdl_list_push_front (list, (void*)9);
  vdl_list_push_front (list, (void*)3);
  vdl_list_push_front (list, (void*)9);
  vdl_list_push_front (list, (void*)9);
  vdl_list_push_front (list, (void*)1);
  vdl_list_push_front (list, (void*)2);
  vdl_list_push_front (list, (void*)2);
  vdl_list_unique (list);
  CHECK_LIST (list, 6, 2, 1, 9, 3, 9, 7);
  vdl_list_unicize (list);
  CHECK_LIST (list, 5, 2, 1, 9, 3, 7);
  vdl_list_sort (list, cmp_int, 0);
  CHECK_LIST (list, 5, 1, 2, 3, 7, 9);

  void **i;
  i = vdl_list_find (list, (void*)2);
  i = vdl_list_erase (list, i);
  CHECK_LIST (list, 4, 1, 3, 7, 9);
  i = vdl_list_find_from (list, i, (void*)7);
  i = vdl_list_erase (list, i);
  CHECK_LIST (list, 3, 1, 3, 9);
  i = vdl_list_find_from (list, i, (void*)10);
  INTERNAL_TEST_ASSERT_EQ (vdl_list_end (list), i);

  vdl_list_clear (list);
  CHECK_LIST (list, 0);

  vdl_list_reverse (list);
  CHECK_LIST (list, 0);
  vdl_list_reverse (list);
  CHECK_LIST (list, 0);

  vdl_list_delete (list);

  return true;
}
