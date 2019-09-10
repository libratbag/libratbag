import subprocess

from test_emulation import TestDevice, MouseData, MM_TO_INCH


class TestSteelseriesDevice2(TestDevice):
    def test_create(self, client):
        id = client.create('steelseries-rival310')
        client.delete(id)

    def test_dpi(self, client, dpi_id=0):
        id = client.create('steelseries-rival310')

        old_dpi = 500
        new_dpi = 1000

        client.set_dpi(id, dpi_id, old_dpi)
        subprocess.run(f'ratbagctl ratbag-emu resolution {str(dpi_id)} dpi set {str(new_dpi)}', shell=True)

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

        client.delete(id)
