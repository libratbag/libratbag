#!/usr/bin/python3
# vim: set expandtab shiftwidth=4 tabstop=4:

import sys
from lxml import etree
import logging

ns = {'svg': 'http://www.w3.org/2000/svg'}
style_query = '//svg:rect[@id=\"{}\"][contains(@style, \"{}\")]'

logger = None

class SVGLogger(logging.Logger):
    def __init__(self, name, level=logging.NOTSET):
        self.success = True
        return super().__init__(name, level)

    def error(self, msg, *args, **kwargs):
        self.success = False
        return super().error(msg, *args, **kwargs)

    @classmethod
    def get_logger(cls, path):
        logging.setLoggerClass(SVGLogger)
        logging.basicConfig(level=logging.DEBUG)
        return logging.getLogger(path)


def check_size(root):
    width = float(root.attrib['width'])
    height = float(root.attrib['height'])
    if not 400 < width < 500:
        logger.error("Width is outside of range: {}".format(width))
    if not 400 < height < 500:
        logger.error("Height is outside of range: {}".format(height))


def check_layers(root):
    """
    Check there are layers (well, groups) for the components we require.
    """
    layer_ids = [g.attrib['id'] for g in root.iterfind('svg:g', ns)]

    for layer in ["Device", "Buttons", "LEDs"]:
        if layer not in layer_ids:
            logger.error("Missing layer: {}".format(layer))


def check_elements(root, prefix, required=0):
    """
    Checks for elements of the form 'prefixN' in the root tag. Any elements
    found must be consecutive or an warning is printed, i.e. if there's a
    'button8' there has to be a 'button7'.

    If required is nonzero, an error is logged for any missing element with
    an index less than required.
    """

    # elements can be paths and rects
    # This includes leaders and lines
    element_ids = [p.attrib['id'] for p in root.xpath('//svg:path', namespaces=ns) if p.attrib['id'].startswith(prefix)]
    element_ids += [p.attrib['id'] for p in root.xpath('//svg:rect', namespaces=ns) if p.attrib['id'].startswith(prefix)]
    element_ids += [g.attrib['id'] for g in root.xpath('//svg:g', namespaces=ns) if g.attrib['id'].startswith(prefix)]

    idx = 0
    highest = -1
    for idx in range(0, 20):
        e = '{}{}'.format(prefix, idx)
        if e in element_ids:
            highest = idx
            previous = '{}{}'.format(prefix, idx - 1)
            if idx > 0 and previous not in element_ids:
                logger.warning("Non-consecutive {}: {}".format(prefix, e))

            leader = '{}{}-leader'.format(prefix, idx)
            if leader not in element_ids:
                logger.error("Missing {} for {}".format(leader, e))
            else:
                element = root.xpath(style_query.format(leader, 'text-align'), namespaces=ns)
                if element is None or len(element) != 1 or element[0] is None:
                    logger.error("Missing style property for {}".format(leader))

            path = '{}{}-path'.format(prefix, idx)
            if path not in element_ids:
                logger.error("Missing {} for {}".format(path, e))
        elif idx < required:
            logger.error("Missing {}: {}".format(prefix, e))

    logger.info("Found {} {}s".format(highest + 1, prefix))


def check_leds(root):
    check_elements(root, "led")


def check_buttons(root):
    check_elements(root, "button", 3)


def check_svg(path):
    svg = etree.parse(path)
    root = svg.getroot()

    check_size(root)
    check_layers(root)
    check_buttons(root)
    check_leds(root)

if __name__ == "__main__":
    success = True
    for path in sys.argv[1:]:
        logger = SVGLogger.get_logger(path)
        check_svg(path)
        if not logger.success:
            success = False

    if not success:
        sys.exit(1)
