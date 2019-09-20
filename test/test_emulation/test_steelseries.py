import subprocess

from time import sleep

from test_emulation import TestDevice, MouseData, MM_TO_INCH


class TestSteelseriesDevice2(TestDevice):
    shortname = 'steelseries-rival310'

    def test_create(self, id, ratbagd, client):
        assert ratbagd.find_device(f'ratbag-emu {id}')

    def test_dpi(self, id, ratbagd, client, dpi_id=0):
        old_dpi = 500
        new_dpi = 1000

        client.set_dpi(id, dpi_id, old_dpi)

        device = ratbagd.find_device(f'ratbag-emu {id}')
        device.active_profile.resolutions[dpi_id].resolution = (new_dpi,)
        device.commit()
        sleep(0.1)

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
