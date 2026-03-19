"""Location code generation and display text formatting."""
from app.constants.enums import LocationType


def build_bed_code(floor: int, room: int, bed: int) -> str:
    """
    Generates the 5-digit bed code per the project spec.
    floor=2, room=207, bed=4  →  "22074"
    """
    floor_str = str(floor)
    room_str = str(room)        # e.g. "207" (already 3 digits by convention)
    bed_str = str(bed)
    return f"{floor_str}{room_str}{bed_str}"


def location_display_text(location_type: str, floor: int | None, room: int | None,
                          bed: int | None, display_name: str) -> str:
    """
    Returns a Korean display string for the robot's current location.
    Examples:
        "2층 207호 4번 침상"
        "세탁실"
    """
    if location_type == LocationType.BED and floor and room and bed:
        return f"{floor}층 {room}호 {bed}번 침상"
    return display_name


NON_BED_DISPLAY: dict[str, str] = {
    LocationType.STATION:      "스테이션",
    LocationType.LAUNDRY:      "세탁실",
    LocationType.WAREHOUSE:    "창고",
    LocationType.SPECIMEN_LAB: "검체실",
    LocationType.EXAM_A:       "검사실 A",
    LocationType.EXAM_B:       "검사실 B",
    LocationType.EXAM_C:       "검사실 C",
}
