import time

# Chap should recognize at least the 3 PyDictKeys objects implied here
# but in terms of reporting key:value pairs associated with the two lines
# it should only report for ones where both the key and the value are
# instances of python str.  Note that this only has been tested to work
# (in terms of chap detecting things) with python 3.5, and is not
# expected to work with python 2.x.
dict1 = { "a":"b", "c":"d", "e":{"f":"g","h":92}, 9:"i" }
dict2 = {}

print("process is ready to have core taken")
time.sleep(3600)
