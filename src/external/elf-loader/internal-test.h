#ifndef INTERNAL_TEST_H
#define INTERNAL_TEST_H

#include <iostream>

#define INTERNAL_TEST_ASSERT_VERBOSE(value,file,line)	\
  if (!(value))						\
    {							\
      std::cerr << file << ":" << line			\
		<< " !" << #value << " "		\
		<< std::endl;				\
      return false;					\
    }

#define INTERNAL_TEST_ASSERT_EQ_VERBOSE(a,b,file,line)	\
  if ((a) != (b))					\
    {							\
      std::cerr << file << ":" << line			\
		<< " " << #a << "!=" << #b		\
		<< std::endl;				\
      return false;					\
    }

#define INTERNAL_TEST_ASSERT(value)			\
  INTERNAL_TEST_ASSERT_VERBOSE(value,__FILE__,__LINE__)
#define INTERNAL_TEST_ASSERT_EQ(a,b)	\
  INTERNAL_TEST_ASSERT_EQ_VERBOSE(a,b,__FILE__,__LINE__)

#endif /* INTERNAL_TEST_H */
