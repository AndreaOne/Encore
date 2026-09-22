from __future__ import annotations

import io
from pathlib import Path
import sys
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

from score_bundle import analyze_musicxml
from score_bundle import choose_part


FIXTURE_PATH = ROOT / "tests" / "fixtures" / "simple.musicxml"


class ScoreBundleTests(unittest.TestCase):
    def test_analyze_musicxml_builds_expected_bundle(self) -> None:
        score = analyze_musicxml(FIXTURE_PATH.read_bytes())
        bundle = choose_part(score, "P1")

        self.assertEqual(score["title"], "Simple Tune")
        self.assertEqual(bundle["part"], ["P1", "Melody"])
        self.assertEqual(bundle["dur"], 3000)
        self.assertEqual(
            bundle["notes"],
            [
                [0, 500, 26163, 1, 1],
                [500, 500, 29366, 1, 1],
                [1000, 1000, 32963, 2, 1],
                [2000, 1000, 34923, 3, 2],
            ],
        )
        self.assertEqual(
            bundle["measures"],
            [
                [1, 0, 1000, 1, 0, 1],
                [2, 1000, 2000, 1, 2, 2],
                [3, 2000, 3000, 2, 3, 3],
            ],
        )
        self.assertEqual(bundle["pages"], [[1, 1, 2], [2, 3, 3]])
        self.assertEqual(bundle["markers"], [[1000, 2, 2, 2]])

    def test_analyze_musicxml_supports_compressed_mxl(self) -> None:
        archive_bytes = io.BytesIO()
        with zipfile.ZipFile(archive_bytes, "w") as archive:
            archive.writestr(
                "META-INF/container.xml",
                """<?xml version="1.0" encoding="UTF-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles>
    <rootfile full-path="score.musicxml" media-type="application/vnd.recordare.musicxml+xml"/>
  </rootfiles>
</container>
""",
            )
            archive.writestr("score.musicxml", FIXTURE_PATH.read_text(encoding="utf-8"))

        score = analyze_musicxml(archive_bytes.getvalue())
        bundle = choose_part(score, "P1")

        self.assertEqual(bundle["part"], ["P1", "Melody"])
        self.assertEqual(len(bundle["notes"]), 4)

    def test_ties_are_merged_into_single_event(self) -> None:
        xml = b"""<?xml version="1.0" encoding="UTF-8"?>
<score-partwise version="3.1">
  <part-list>
    <score-part id="P1"><part-name>Solo</part-name></score-part>
  </part-list>
  <part id="P1">
    <measure number="1">
      <attributes>
        <divisions>1</divisions>
        <time><beats>2</beats><beat-type>4</beat-type></time>
      </attributes>
      <direction><sound tempo="120"/></direction>
      <note>
        <pitch><step>C</step><octave>4</octave></pitch>
        <duration>2</duration>
        <tie type="start"/>
      </note>
    </measure>
    <measure number="2">
      <note>
        <pitch><step>C</step><octave>4</octave></pitch>
        <duration>2</duration>
        <tie type="stop"/>
      </note>
    </measure>
  </part>
</score-partwise>
"""

        score = analyze_musicxml(xml)
        bundle = choose_part(score, "P1")

        self.assertEqual(bundle["notes"], [[0, 2000, 26163, 1, 1]])

    def test_duplicate_measure_numbers_do_not_merge_event_bounds(self) -> None:
        xml = b"""<?xml version="1.0" encoding="UTF-8"?>
<score-partwise version="3.1">
  <part-list>
    <score-part id="P1"><part-name>Solo</part-name></score-part>
  </part-list>
  <part id="P1">
    <measure number="0">
      <attributes>
        <divisions>1</divisions>
        <time><beats>3</beats><beat-type>4</beat-type></time>
      </attributes>
      <direction><sound tempo="120"/></direction>
      <note><pitch><step>C</step><octave>4</octave></pitch><duration>1</duration></note>
      <note><pitch><step>D</step><octave>4</octave></pitch><duration>1</duration></note>
      <note><pitch><step>E</step><octave>4</octave></pitch><duration>1</duration></note>
    </measure>
    <measure number="0">
      <note><pitch><step>F</step><octave>4</octave></pitch><duration>1</duration></note>
      <note><pitch><step>G</step><octave>4</octave></pitch><duration>1</duration></note>
      <note><pitch><step>A</step><octave>4</octave></pitch><duration>1</duration></note>
    </measure>
  </part>
</score-partwise>
"""

        score = analyze_musicxml(xml)
        bundle = choose_part(score, "P1")

        self.assertEqual(
            bundle["measures"],
            [
                [0, 0, 1500, 1, 0, 2],
                [0, 1500, 3000, 1, 3, 5],
            ],
        )


if __name__ == "__main__":
    unittest.main()
