import fcntl
import libevdev
import requests
import shutil
import subprocess
import os
import pytest
import threading

from pathlib import Path
from time import sleep


MM_TO_INCH = 0.0393700787


class RatbagemuClient(object):
    def __init__(self, url='http://localhost:8080'):
        self.url = url


    # Internal wrappers around the http requests
    def _get(self, path):
        return requests.get(f'{self.url}{path}')

    def _post(self, path, data=None, json=None):
        return requests.post(f'{self.url}{path}', data=data, json=json)

    def _delete(self, path):
        return requests.delete(f'{self.url}{path}')

    def _put(self, path, data=None, json=None):
        return requests.put(f'{self.url}{path}', data=data, json=json)

    '''
    ratbag-emu functions
    '''
    def create(self, shortname):
        data = {
            'shortname': shortname
        }
        response = self._post('/devices/add', json=data)
        assert response.status_code == 201
        return response.json()['id']

    def delete(self, id):
        response = self._delete(f'/devices/{id}')
        assert response.status_code == 204

    def get_dpi(self, id, dpi_id):
        response = self._get(f'/devices/{id}/phys_props/dpi/{dpi_id}')
        assert response.status_code == 200
        return response.json()

    def set_dpi(self, id, dpi_id, new_dpi):
        response = self._put(f'/devices/{id}/phys_props/dpi/{dpi_id}', json=new_dpi)
        assert response.status_code == 200

    def get_input_nodes(self, id):
        response = self._get(f'/devices/{id}')
        if response.status_code == 200 and 'input_nodes' in response.json():
            return response.json()['input_nodes']

    def send_event(self, id, event):
        response = self._post(f'/devices/{id}/event', json=event)
        assert response.status_code == 200


class TestDevice(object):
    def reload_udev_rules(self):
        subprocess.run('udevadm control --reload-rules'.split())

    @pytest.fixture(scope='session', autouse=True)
    def udev_rules(self):
        rules_file = '61-ratbag-emu-ignore-test-devices.rules'
        rules_dir = Path('/run/udev/rules.d')

        rules_src = Path('test/test_emulation/rules.d') / rules_file
        rules_dest = rules_dir / rules_file

        rules_dir.mkdir(exist_ok=True)
        shutil.copyfile(rules_src, rules_dest)
        self.reload_udev_rules()

        yield

        if rules_dest.is_file():
            rules_dest.unlink()
            self.reload_udev_rules()

    @pytest.fixture(autouse=True)
    def client(self):
        yield RatbagemuClient()

    @pytest.fixture
    def id(self, client):
        id = client.create(self.shortname)

        yield id

        client.delete(id)

    def _simulate(self, client, id, data):
        input_nodes = client.get_input_nodes(id)

        # Open the event nodes
        event_nodes = []
        for node in set(input_nodes):
            fd = open(node, 'rb')
            fcntl.fcntl(fd, fcntl.F_SETFL, os.O_NONBLOCK)
            event_nodes.append(libevdev.Device(fd))

        events = []
        def collect_events(stop):
            nonlocal events
            while not stop.is_set():
                for node in event_nodes:
                    events += list(node.events())

        stop_event_thread = threading.Event()
        event_thread = threading.Thread(target=collect_events, args=(stop_event_thread,))
        event_thread.start()

        client.send_event(id, data)

        sleep(1)
        stop_event_thread.set()
        event_thread.join()

        for node in event_nodes:
            node.fd.close()

        received = MouseData()
        for e in events:
            if e.matches(libevdev.EV_REL.REL_X):
                received.x += e.value
            elif e.matches(libevdev.EV_REL.REL_Y):
                received.y += e.value

        return received


class MouseData(object):
    '''
    Holds event data
    '''

    def __init__(self, x=0, y=0):
        self.x = x
        self.y = y

    @staticmethod
    def from_mm(dpi, x=0, y=0):
        return MouseData(x=int(x * MM_TO_INCH * dpi),
                         y=int(y * MM_TO_INCH * dpi))

