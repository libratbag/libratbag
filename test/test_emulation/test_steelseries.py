import subprocess

from test_emulation import TestDevice, MouseData, MM_TO_INCH


class TestSteelseriesDevice2(TestDevice):
    shortname = 'steelseries-rival310'

    def test_create(self, id, client):
        subprocess.check_call(f"ratbagctl 'ratbag-emu {id}' info >/dev/null", shell=True)

    def test_dpi(self, id, client, dpi_id=0):
        old_dpi = 500
        new_dpi = 1000

        client.set_dpi(id, dpi_id, old_dpi)
        subprocess.check_call(f"ratbagctl 'ratbag-emu {id}' resolution {str(dpi_id)} dpi set {str(new_dpi)}", shell=True)

        x = y = 5
        data = [
            {
                'start': 0,
                'end': 500,
                'action': {
                    'type': 'xy',
                    'x': x,
                    'y': y
                }
            }
        ]
        received = self._simulate(client, id, data)

        expected = MouseData.from_mm(new_dpi, x=x, y=y)

        assert expected.x - 1 <= received.x <= expected.x + 1
        assert expected.y - 1 <= received.y <= expected.y + 1
