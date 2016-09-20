import unittest

from . import JunitXml, TestCase


class TestJunitXml(unittest.TestCase):
	""" tests for the JunitXml class """

	def test_basic_usage(self):
		""" test the basic usage of the JunitXml class """
		test_cases = []
		for i in range(0, 5):
			type_c = ""
			if i % 2 == 0:
				type_c = "failure"
			test_cases.append(TestCase(i, "%scontents" % i,
				type_c))
		junit = JunitXml("demo test example", test_cases)
		self.assertEqual(junit.total_tests, 5)
		self.assertEqual(junit.total_failures, 3)
		self.assertTrue(junit.dump())


if __name__ == "__main__":
	unittest.main()
