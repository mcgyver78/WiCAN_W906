"""Checks the layouts in display/layouts, the views people share for the display.

Every file has to be a layout the display reads without a problem and without a warning: a JSON object of
the format in display/components/core/layout.h, within its limits. The built-in layout of the Sprinter
W906 also has to name every value of the vehicle profile exactly once.

The display checks a layout itself (layout_parse(), host test display/test/test_layout.c). This is the same
rule book a second time, in another language, for files nobody has loaded into a display yet. It is
stricter in three places: a member named twice is a problem (the display takes the first one, most other
readers the last), a file that is not UTF-8 is one (the display passes the bytes on), and so is everything
the display would only warn about.

  python -m unittest -v          in display/tools, as the CI does
"""
import copy
import glob
import json
import math
import os
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
LAYOUTS = os.path.join(HERE, "..", "layouts")
W906_LAYOUT = os.path.join(LAYOUTS, "w906_default.json")
W906_PROFILE = os.path.join(HERE, "..", "..", "vehicle_profiles", "mercedes", "sprinter_w906_om651.json")

# The limits of layout.h and json.h
FORMAT = "wican-display-layout"
VERSION = 1
TEXT_MAX = 16384
PAGES_MAX = 12
ITEMS_MAX = 6
MAP_MAX = 8
NAME_BYTES = 32
KEY_BYTES = 32
TITLE_BYTES = 24
UNIT_BYTES = 8
MAP_RAW_BYTES = 11
MAP_TEXT_BYTES = 23
NUMBER_CHARACTERS = 47
TOKENS = 4096
DEPTH = 8
WIDGETS = ("number", "arc", "bar", "state")
LIMITS = ("min", "max", "warn_lo", "warn_hi", "crit_lo", "crit_hi")


class Members(list):
    """The members of a JSON object as (name, value) in the order of the text: a dict would hide a
    name that stands twice."""

    def get(self, name):
        for key, value in self:
            if key == name:
                return value
        return None

    def has(self, name):
        return any(key == name for key, _ in self)


class Number(float):
    """A number as it was parsed, with what it was: the display wants whole numbers in places."""

    def __new__(cls, literal, whole):
        number = super().__new__(cls, literal)
        number.literal = literal
        number.whole = whole
        return number


def read(text):
    """The JSON value of a text, objects as Members and numbers as Number. ValueError if it is no JSON."""
    def refuse(constant):
        raise ValueError("%s is no JSON" % constant)

    return json.loads(text, object_pairs_hook=Members, parse_constant=refuse,
                      parse_int=lambda literal: Number(literal, True),
                      parse_float=lambda literal: Number(literal, False))


def tokens_and_depth(value, depth=0):
    """What the JSON reader of the display needs for a value: its tokens and how deep its containers nest."""
    if isinstance(value, Members):
        counted = [tokens_and_depth(member, depth + 1) for _, member in value]
        return 1 + len(value) + sum(t for t, _ in counted), max([depth + 1] + [d for _, d in counted])
    if isinstance(value, list):
        counted = [tokens_and_depth(element, depth + 1) for element in value]
        return 1 + sum(t for t, _ in counted), max([depth + 1] + [d for _, d in counted])
    return 1, depth


def is_number(value):
    return isinstance(value, Number)


def text_problem(value, limit):
    """What is wrong with a text of the layout, None if nothing."""
    if not isinstance(value, str):
        return "not a text"
    try:
        encoded = value.encode("utf-8")
    except UnicodeEncodeError:
        return "half a surrogate pair"
    if any(byte < 0x20 or byte == 0x7F for byte in encoded):
        return "control character"
    if len(encoded) > limit:
        return "too long: %d bytes, at most %d" % (len(encoded), limit)
    return None


def number_problem(value):
    if not is_number(value):
        return "not a number"
    if len(value.literal) > NUMBER_CHARACTERS:
        return "written with more than %d characters" % NUMBER_CHARACTERS
    if not math.isfinite(value):
        return "not finite"
    return None


def doubled(members):
    """The names that stand more than once in an object."""
    names = [name for name, _ in members]
    return sorted(set(name for name in names if names.count(name) > 1))


def item_problems(item, where):
    found = []
    if not isinstance(item, Members):
        return ["%s: not an object" % where]
    found += ["%s: %s stands twice" % (where, name) for name in doubled(item)]

    if not item.has("key"):
        found.append("%s.key: missing" % where)
    elif item.get("key") == "":
        found.append("%s.key: empty" % where)
    for name, limit in (("key", KEY_BYTES), ("label", TITLE_BYTES), ("unit", UNIT_BYTES)):
        if item.has(name) and text_problem(item.get(name), limit):
            found.append("%s.%s: %s" % (where, name, text_problem(item.get(name), limit)))

    if item.has("dec"):
        decimals = item.get("dec")
        if not is_number(decimals) or not decimals.whole or not 0 <= decimals <= 3:
            found.append("%s.dec: not an integer 0 to 3" % where)

    for name in ("scale",) + LIMITS:
        if item.has(name) and number_problem(item.get(name)):
            found.append("%s.%s: %s" % (where, name, number_problem(item.get(name))))
    if is_number(item.get("scale")) and item.get("scale") == 0:
        found.append("%s.scale: zero" % where)
    low, high = item.get("min"), item.get("max")
    if is_number(low) and is_number(high) and not low < high:
        found.append("%s.min: min is not below max" % where)

    entries = 0
    if item.has("map"):
        texts = item.get("map")
        if not isinstance(texts, Members):
            found.append("%s.map: not an object" % where)
        else:
            entries = len(texts)
            if entries > MAP_MAX:
                found.append("%s.map: %d entries, at most %d" % (where, entries, MAP_MAX))
            for position, (raw, shown) in enumerate(texts):
                for text, limit in ((raw, MAP_RAW_BYTES), (shown, MAP_TEXT_BYTES)):
                    if text_problem(text, limit):
                        found.append("%s.map[%d]: %s" % (where, position, text_problem(text, limit)))

    # What the display accepts with a warning does not belong into a shared layout
    if item.has("widget"):
        widget = item.get("widget")
        if not isinstance(widget, str):
            found.append("%s.widget: not a text" % where)
        elif widget not in WIDGETS:
            found.append("%s.widget: unknown widget" % where)
        elif widget in ("arc", "bar") and not (item.has("min") and item.has("max")):
            found.append("%s.widget: arc or bar without min and max" % where)
        elif widget == "state" and entries == 0:
            found.append("%s.widget: state without map" % where)
    return found


def page_problems(page, where):
    found = []
    if not isinstance(page, Members):
        return ["%s: not an object" % where]
    found += ["%s: %s stands twice" % (where, name) for name in doubled(page)]
    if page.has("title") and text_problem(page.get("title"), TITLE_BYTES):
        found.append("%s.title: %s" % (where, text_problem(page.get("title"), TITLE_BYTES)))
    if page.has("hidden") and not isinstance(page.get("hidden"), bool):
        found.append("%s.hidden: not a boolean" % where)

    items = page.get("items")
    if not page.has("items"):
        found.append("%s.items: missing" % where)
    elif not isinstance(items, list) or isinstance(items, Members):
        found.append("%s.items: not an array" % where)
    elif not 1 <= len(items) <= ITEMS_MAX:
        found.append("%s.items: %d items, 1 to %d" % (where, len(items), ITEMS_MAX))
    else:
        for position, item in enumerate(items):
            found += item_problems(item, "%s.items[%d]" % (where, position))
    return found


def problems(data):
    """Texts of everything that is wrong with the bytes of a layout file, empty if the display takes it
    without a problem and without a warning."""
    if len(data) > TEXT_MAX:
        return ["text too long: %d bytes, at most %d" % (len(data), TEXT_MAX)]
    try:
        layout = read(data.decode("utf-8"))
    except (ValueError, RecursionError) as error:
        return ["not valid JSON: %s" % error]
    if not isinstance(layout, Members):
        return ["not a JSON object"]

    found = ["%s stands twice" % name for name in doubled(layout)]
    tokens, depth = tokens_and_depth(layout)
    if tokens > TOKENS:
        found.append("%d JSON tokens, the display reads %d" % (tokens, TOKENS))
    if depth > DEPTH:
        found.append("nested %d deep, the display reads %d" % (depth, DEPTH))

    if layout.get("format") != FORMAT:
        found.append("format: not %s" % FORMAT)
    version = layout.get("v")
    if not is_number(version) or not version.whole:
        found.append("v: not an integer")
    elif not 1 <= version <= VERSION:
        found.append("v: not 1 to %d" % VERSION)
    for name in ("name", "profile_hint"):
        if layout.has(name) and text_problem(layout.get(name), NAME_BYTES):
            found.append("%s: %s" % (name, text_problem(layout.get(name), NAME_BYTES)))

    pages = layout.get("pages")
    if not layout.has("pages"):
        found.append("pages: missing")
    elif not isinstance(pages, list) or isinstance(pages, Members):
        found.append("pages: not an array")
    elif not 1 <= len(pages) <= PAGES_MAX:
        found.append("pages: %d pages, 1 to %d" % (len(pages), PAGES_MAX))
    else:
        for position, page in enumerate(pages):
            found += page_problems(page, "pages[%d]" % position)
    return found


def keys(data):
    """The keys of all items of a layout that problems() found nothing wrong with, in their order."""
    return [item.get("key") for page in read(data.decode("utf-8")).get("pages") for item in page.get("items")]


def profile_values(data):
    """The names of the values a vehicle profile delivers."""
    return [name for pid in json.loads(data.decode("utf-8"))["pids"] for name in pid["parameters"]]


def coverage_problems(layout_keys, values):
    """Texts of everything that keeps a layout from naming each value of a profile exactly once."""
    found = ["%s is not on any page" % value for value in values if value not in layout_keys]
    found += ["%s is on %d pages or places" % (value, layout_keys.count(value))
              for value in values if layout_keys.count(value) > 1]
    found += ["%s is not a value of the profile" % key for key in dict.fromkeys(layout_keys) if key not in values]
    return found


def file_bytes(path):
    with open(path, "rb") as file:
        return file.read()


def w906():
    """The built-in layout as Python data."""
    return json.loads(file_bytes(W906_LAYOUT).decode("utf-8"))


def encoded(layout):
    return json.dumps(layout, ensure_ascii=False).encode("utf-8")


def with_item(**members):
    """A layout of one page with one item that has the key K and these members."""
    return encoded({"format": FORMAT, "v": 1, "pages": [{"items": [dict({"key": "K"}, **members)]}]})


class Layouts(unittest.TestCase):
    def test_there_is_the_built_in_layout(self):
        names = [os.path.basename(path) for path in glob.glob(os.path.join(LAYOUTS, "*"))]
        self.assertIn("w906_default.json", names)
        self.assertEqual([name for name in names if not name.endswith(".json")], [])

    def test_every_layout_is_of_the_format_and_within_its_limits(self):
        for path in sorted(glob.glob(os.path.join(LAYOUTS, "*"))):
            with self.subTest(os.path.basename(path)):
                self.assertEqual(problems(file_bytes(path)), [])

    def test_the_w906_layout_names_each_value_of_the_profile_exactly_once(self):
        values = profile_values(file_bytes(W906_PROFILE))
        self.assertEqual(len(values), 35)
        self.assertEqual(len(set(values)), 35)
        self.assertEqual(coverage_problems(keys(file_bytes(W906_LAYOUT)), values), [])

    def test_the_w906_layout_is_the_one_of_the_design(self):
        layout = w906()
        self.assertEqual([page["title"] for page in layout["pages"]],
                         ["Motor", "Ladeluft", "Abgas", "DPF", "Kraftstoff", "AGR/Luft", "Sonstiges"])
        self.assertEqual([len(page["items"]) for page in layout["pages"]], [4, 6, 6, 6, 6, 5, 2])
        items = [item for page in layout["pages"] for item in page["items"]]
        self.assertEqual([item["key"] for item in items if not item.get("label")], [])
        # Delivered without limits: the numbers in the drafts were guesses
        self.assertEqual([item["key"] for item in items
                          if any(name in item for name in ("warn_lo", "warn_hi", "crit_lo", "crit_hi", "scale"))], [])
        self.assertEqual([page["title"] for page in layout["pages"] if page.get("hidden")], [])


class CounterCheck(unittest.TestCase):
    """The checks above fail when they have to: on a layout with a key removed, misspelt or doubled, and
    on small layouts that break one rule each. The layout that is broken here is made from the profile and
    not from the file in display/layouts: a mistake in that file shows in the tests above, not here."""

    def setUp(self):
        self.values = profile_values(file_bytes(W906_PROFILE))

    def layout(self):
        """Each value of the profile once, six to a page."""
        return {"format": FORMAT, "v": 1, "pages": [{"items": [{"key": key} for key in self.values[first:first + 6]]}
                                                    for first in range(0, len(self.values), 6)]}

    def coverage(self, layout):
        data = encoded(layout)
        self.assertEqual(problems(data), [])
        return coverage_problems(keys(data), self.values)

    def test_a_layout_with_each_value_once_is_fine(self):
        layout = self.layout()
        self.assertEqual([len(page["items"]) for page in layout["pages"]], [6, 6, 6, 6, 6, 5])
        self.assertEqual(self.coverage(layout), [])

    def test_the_order_of_the_keys_does_not_matter(self):
        layout = self.layout()
        layout["pages"].reverse()
        layout["pages"][2]["items"].reverse()
        self.assertEqual(self.coverage(layout), [])

    def test_a_removed_key_is_noticed(self):
        layout = self.layout()
        removed = layout["pages"][3]["items"].pop(2)
        self.assertEqual(removed["key"], self.values[20])
        self.assertEqual(self.coverage(layout), ["%s is not on any page" % self.values[20]])

    def test_a_removed_page_is_noticed(self):
        layout = self.layout()
        del layout["pages"][5]
        self.assertEqual(self.coverage(layout), ["%s is not on any page" % value for value in self.values[30:]])

    def test_a_misspelt_key_is_noticed(self):
        layout = self.layout()
        layout["pages"][0]["items"][1]["key"] = self.values[1] + "S"
        self.assertEqual(self.coverage(layout), ["%s is not on any page" % self.values[1],
                                                 "%sS is not a value of the profile" % self.values[1]])

    def test_a_key_in_other_letters_is_noticed(self):
        layout = self.layout()
        layout["pages"][0]["items"][0]["key"] = self.values[0].lower()
        self.assertEqual(self.coverage(layout), ["%s is not on any page" % self.values[0],
                                                 "%s is not a value of the profile" % self.values[0].lower()])

    def test_a_key_cut_short_is_noticed(self):
        layout = self.layout()
        layout["pages"][0]["items"][0]["key"] = self.values[0][:-1]
        self.assertEqual(self.coverage(layout), ["%s is not on any page" % self.values[0],
                                                 "%s is not a value of the profile" % self.values[0][:-1]])

    def test_a_doubled_key_is_noticed(self):
        layout = self.layout()
        layout["pages"][5]["items"].append(copy.deepcopy(layout["pages"][0]["items"][0]))
        self.assertEqual(self.coverage(layout), ["%s is on 2 pages or places" % self.values[0]])

    def test_a_key_doubled_on_its_own_page_is_noticed(self):
        layout = self.layout()
        layout["pages"][5]["items"].append(copy.deepcopy(layout["pages"][5]["items"][4]))
        self.assertEqual(self.coverage(layout), ["%s is on 2 pages or places" % self.values[34]])

    def test_a_key_named_three_times_is_noticed(self):
        layout = self.layout()
        layout["pages"].append({"items": [{"key": self.values[3]}, {"key": self.values[3]}]})
        self.assertEqual(self.coverage(layout), ["%s is on 3 pages or places" % self.values[3]])

    def test_a_key_in_place_of_another_is_noticed(self):
        layout = self.layout()
        layout["pages"][1]["items"][0]["key"] = self.values[7]
        self.assertEqual(self.coverage(layout), ["%s is not on any page" % self.values[6],
                                                 "%s is on 2 pages or places" % self.values[7]])

    def test_a_key_the_profile_does_not_have_is_noticed(self):
        layout = self.layout()
        layout["pages"][5]["items"].append({"key": "@BATT_V"})
        self.assertEqual(self.coverage(layout), ["@BATT_V is not a value of the profile"])

    def test_every_broken_layout_is_reported(self):
        page = {"items": [{"key": "K"}]}
        head = '{"format":"wican-display-layout","v":1,'
        broken = {
            "empty file": b"",
            "no JSON": b'{"format":"wican-display-layout","v":1,"pages":[',
            "an array": b"[]",
            "text behind the layout": head.encode() + b'"pages":[{"items":[{"key":"K"}]}]} x',
            "not UTF-8": head.encode() + b'"pages":[{"items":[{"key":"\xff"}]}]}',
            "too long": head.encode() + b'"pages":[{"items":[{"key":"K"}]}]}' + b" " * TEXT_MAX,
            "NaN": head.encode() + b'"pages":[{"items":[{"key":"K","min":NaN}]}]}',
            "nested 9 deep": head.encode() + b'"pages":[{"items":[{"key":"K","later":[[[[1]]]]}]}]}',
            "4097 tokens": head.encode() + b'"later":[' + b"1," * 4081 + b'1],"pages":[{"items":[{"key":"K"}]}]}',
            "format missing": encoded({"v": 1, "pages": [page]}),
            "another format": encoded({"format": "wican-display-layout2", "v": 1, "pages": [page]}),
            "v missing": encoded({"format": FORMAT, "pages": [page]}),
            "v 2": encoded({"format": FORMAT, "v": 2, "pages": [page]}),
            "v 0": encoded({"format": FORMAT, "v": 0, "pages": [page]}),
            "v 1.0": head.replace('"v":1,', '"v":1.0,').encode() + b'"pages":[{"items":[{"key":"K"}]}]}',
            "v as text": encoded({"format": FORMAT, "v": "1", "pages": [page]}),
            "v true": encoded({"format": FORMAT, "v": True, "pages": [page]}),
            "name of 33 bytes": encoded({"format": FORMAT, "v": 1, "name": "n" * 33, "pages": [page]}),
            "name is a number": encoded({"format": FORMAT, "v": 1, "name": 5, "pages": [page]}),
            "profile_hint of 33 bytes": encoded({"format": FORMAT, "v": 1, "profile_hint": "h" * 33, "pages": [page]}),
            "format twice": head.encode() + b'"format":"x","pages":[{"items":[{"key":"K"}]}]}',
            "pages missing": encoded({"format": FORMAT, "v": 1}),
            "pages is an object": encoded({"format": FORMAT, "v": 1, "pages": {}}),
            "no page": encoded({"format": FORMAT, "v": 1, "pages": []}),
            "13 pages": encoded({"format": FORMAT, "v": 1, "pages": [page] * 13}),
            "a page is a number": encoded({"format": FORMAT, "v": 1, "pages": [5]}),
            "title of 25 bytes": encoded({"format": FORMAT, "v": 1, "pages": [dict(page, title="t" * 25)]}),
            "title of 13 characters of two bytes": encoded({"format": FORMAT, "v": 1, "pages": [dict(page, title="ä" * 13)]}),
            "hidden as text": encoded({"format": FORMAT, "v": 1, "pages": [dict(page, hidden="true")]}),
            "hidden 0": encoded({"format": FORMAT, "v": 1, "pages": [dict(page, hidden=0)]}),
            "items missing": encoded({"format": FORMAT, "v": 1, "pages": [{"title": "T"}]}),
            "items is an object": encoded({"format": FORMAT, "v": 1, "pages": [{"items": {}}]}),
            "no item": encoded({"format": FORMAT, "v": 1, "pages": [{"items": []}]}),
            "7 items": encoded({"format": FORMAT, "v": 1, "pages": [{"items": [{"key": "K"}] * 7}]}),
            "an item is a text": encoded({"format": FORMAT, "v": 1, "pages": [{"items": ["K"]}]}),
            "title twice": head.encode() + b'"pages":[{"title":"A","title":"B","items":[{"key":"K"}]}]}',
            "key missing": encoded({"format": FORMAT, "v": 1, "pages": [{"items": [{"label": "L"}]}]}),
            "key empty": with_item(key=""),
            "key of 33 bytes": with_item(key="k" * 33),
            "key is a number": with_item(key=5),
            "key twice": head.encode() + b'"pages":[{"items":[{"key":"K","key":"L"}]}]}',
            "label of 25 bytes": with_item(label="l" * 25),
            "label is null": with_item(label=None),
            "label with a line break": with_item(label="a\nb"),
            "label with the delete character": with_item(label="a\x7fb"),
            "label with half a surrogate pair": head.encode() + b'"pages":[{"items":[{"key":"K","label":"\\ud83d"}]}]}',
            "unit of 9 bytes": with_item(unit="u" * 9),
            "unit of 9 bytes in 8 characters": with_item(unit="°C/100km"),
            "dec 4": with_item(dec=4),
            "dec -1": with_item(dec=-1),
            "dec 1.5": with_item(dec=1.5),
            "dec 2.0": head.encode() + b'"pages":[{"items":[{"key":"K","dec":2.0}]}]}',
            "dec as text": with_item(dec="1"),
            "dec true": with_item(dec=True),
            "scale 0": with_item(scale=0),
            "scale as text": with_item(scale="0.001"),
            "scale too large": head.encode() + b'"pages":[{"items":[{"key":"K","scale":1e999}]}]}',
            "min equals max": with_item(min=5, max=5),
            "min above max": with_item(min=6, max=5),
            "min as text": with_item(min="0"),
            "max true": with_item(max=True),
            "warn_lo as text": with_item(warn_lo="1"),
            "warn_hi null": with_item(warn_hi=None),
            "crit_lo as array": with_item(crit_lo=[1]),
            "crit_hi too large": head.encode() + b'"pages":[{"items":[{"key":"K","crit_hi":-1e999}]}]}',
            "a number of 48 characters": head.encode() + b'"pages":[{"items":[{"key":"K","min":1.' + b"0" * 46 + b"}]}]}",
            "widget is a number": with_item(widget=5),
            "unknown widget": with_item(widget="gauge", min=0, max=1),
            "widget in other letters": with_item(widget="Arc", min=0, max=1),
            "arc without max": with_item(widget="arc", min=0),
            "bar without min": with_item(widget="bar", max=1),
            "state without map": with_item(widget="state"),
            "state with an empty map": with_item(widget="state", map={}),
            "map is an array": with_item(map=["1", "an"]),
            "9 map entries": with_item(map={str(n): "t" for n in range(9)}),
            "map value of 12 bytes": with_item(map={"v" * 12: "t"}),
            "map text of 24 bytes": with_item(map={"1": "t" * 24}),
            "map text is a number": with_item(map={"1": 5}),
            "map text with a tab": with_item(map={"1": "a\tb"}),
        }
        for name, data in broken.items():
            with self.subTest(name):
                self.assertNotEqual(problems(data), [])

    def test_every_layout_at_a_limit_is_accepted(self):
        page = {"items": [{"key": "K"}]}
        fine = {
            "the smallest layout": encoded({"format": FORMAT, "v": 1, "pages": [page]}),
            "a text of 16384 bytes": encoded({"format": FORMAT, "v": 1, "pages": [page]}).ljust(TEXT_MAX),
            # 15 tokens of the layout and 4081 numbers
            "4096 tokens": ('{"format":"wican-display-layout","v":1,"later":[' + "1," * 4080
                            + '1],"pages":[{"items":[{"key":"K"}]}]}').encode(),
            "nested 8 deep": b'{"format":"wican-display-layout","v":1,"pages":[{"items":[{"key":"K","later":[[[1]]]}]}]}',
            "names of 32 bytes": encoded({"format": FORMAT, "v": 1, "name": "n" * 32, "profile_hint": "h" * 32, "pages": [page]}),
            "12 pages": encoded({"format": FORMAT, "v": 1, "pages": [page] * 12}),
            "6 items": encoded({"format": FORMAT, "v": 1, "pages": [{"items": [{"key": "K"}] * 6}]}),
            "title of 24 bytes, hidden": encoded({"format": FORMAT, "v": 1, "pages": [dict(page, title="ä" * 12, hidden=True)]}),
            "an unknown member": encoded({"format": FORMAT, "v": 1, "author": {"name": 5}, "pages": [dict(page, colour="red")]}),
            "key of 32 bytes": with_item(key="k" * 32),
            "label of 24 bytes": with_item(label="l" * 24),
            "unit of 8 bytes": with_item(unit="°C/100m"),
            "an empty label and an empty unit": with_item(label="", unit=""),
            "dec 0": with_item(dec=0),
            "dec 3": with_item(dec=3),
            "a negative scale": with_item(scale=-0.001),
            "min just below max": with_item(min=-0.5, max=-0.25),
            "limits in any order": with_item(warn_lo=9, warn_hi=1, crit_lo=8, crit_hi=2),
            "a number of 47 characters": b'{"format":"wican-display-layout","v":1,"pages":[{"items":[{"key":"K","min":1.' + b"0" * 45 + b"}]}]}",
            "widget number": with_item(widget="number"),
            "arc with min and max": with_item(widget="arc", min=0, max=5000),
            "bar with min and max": with_item(widget="bar", min=0, max=100),
            "state with a map": with_item(widget="state", map={"1": "inaktiv", "*": "aktiv"}),
            "a map of 8 entries at their limits": with_item(map=dict({str(n): "t" for n in range(7)}, **{"v" * 11: "t" * 23})),
            "a surrogate pair": b'{"format":"wican-display-layout","v":1,"pages":[{"items":[{"key":"K","label":"\\ud83d\\ude00"}]}]}',
        }
        for name, data in fine.items():
            with self.subTest(name):
                self.assertEqual(problems(data), [])

    def test_a_problem_names_its_place(self):
        self.assertEqual(problems(with_item(dec=4)), ["pages[0].items[0].dec: not an integer 0 to 3"])
        self.assertEqual(problems(with_item(map={"1": "an", "0": "t" * 24})),
                         ["pages[0].items[0].map[1]: too long: 24 bytes, at most 23"])
        self.assertEqual(problems(encoded({"format": FORMAT, "v": 1, "pages": [{"items": [{"key": "K"}]}, {"hidden": 1, "items": [{"key": "K"}]}]})),
                         ["pages[1].hidden: not a boolean"])


if __name__ == "__main__":
    unittest.main()
