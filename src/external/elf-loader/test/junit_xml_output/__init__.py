#!/usr/bin/env python
import xml.etree.ElementTree as ET
import xml.dom.minidom

__version__ = "0.0.2"


class JunitXml(object):
	""" A class which is designed to create a junit test xml file.
	    Note: currently this class is designed to return the junit xml file
	    in a string format (through the dump method).
	"""
	def __init__(self, testsuit_name, test_cases, total_tests=None, total_failures=None):
		self.testsuit_name = testsuit_name
		self.test_cases = test_cases
		self.failing_test_cases = self._get_failing_test_cases()
		self.total_tests = total_tests
		self.total_failures = total_failures
		if total_tests is None:
			self.total_tests = len(self.test_cases)
		if total_failures is None:
			self.total_failures = len(self.failing_test_cases)
		self.root = ET.Element("testsuite",
			{
				"name" : unicode(self.testsuit_name),
				"failures": unicode(self.total_failures),
				"tests" : unicode(self.total_tests)
			}
		)
		self.build_junit_xml()

	def _get_failing_test_cases(self):
		return set([case for case in self.test_cases if
			case.is_failure()])

	def build_junit_xml(self):
		""" create the xml tree from the given testsuite name and
		    testcase
		"""
		for case in self.test_cases:
			test_case_element = ET.SubElement(self.root,
				"testcase", {"name" : unicode(case.name)})
			if case.is_failure():
				failure_element = ET.Element("failure")
				failure_element.text = case.contents
				test_case_element.append(failure_element)

	def dump(self, pretty=True):
		""" returns a string representation of the junit xml tree. """
		out = ET.tostring(self.root)
		if pretty:
			dom = xml.dom.minidom.parseString(out)
			out = dom.toprettyxml()
		return out


class TestCase(object):
	""" A junit test case representation class.
            The JunitXml accepts a set of these and uses them to create
	    the junit test xml tree
	"""
	def __init__(self, name, contents, test_type=""):
		self.name = name
		self.contents = contents
		self.test_type = test_type

	def is_failure(self):
		""" returns True if this test case is a 'failure' type """
		return self.test_type == "failure"
