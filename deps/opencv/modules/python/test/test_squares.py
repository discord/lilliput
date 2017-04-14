#!/usr/bin/env python

'''
Simple "Square Detector" program.

Loads several images sequentially and tries to find squares in each image.
'''

# Python 2/3 compatibility
import sys
PY3 = sys.version_info[0] == 3

if PY3:
    xrange = range

import numpy as np
import cv2


def angle_cos(p0, p1, p2):
    d1, d2 = (p0-p1).astype('float'), (p2-p1).astype('float')
    return abs( np.dot(d1, d2) / np.sqrt( np.dot(d1, d1)*np.dot(d2, d2) ) )

def find_squares(img):
    img = cv2.GaussianBlur(img, (5, 5), 0)
    squares = []
    for gray in cv2.split(img):
        for thrs in xrange(0, 255, 26):
            if thrs == 0:
                bin = cv2.Canny(gray, 0, 50, apertureSize=5)
                bin = cv2.dilate(bin, None)
            else:
                retval, bin = cv2.threshold(gray, thrs, 255, cv2.THRESH_BINARY)
            bin, contours, hierarchy = cv2.findContours(bin, cv2.RETR_LIST, cv2.CHAIN_APPROX_SIMPLE)
            for cnt in contours:
                cnt_len = cv2.arcLength(cnt, True)
                cnt = cv2.approxPolyDP(cnt, 0.02*cnt_len, True)
                if len(cnt) == 4 and cv2.contourArea(cnt) > 1000 and cv2.isContourConvex(cnt):
                    cnt = cnt.reshape(-1, 2)
                    max_cos = np.max([angle_cos( cnt[i], cnt[(i+1) % 4], cnt[(i+2) % 4] ) for i in xrange(4)])
                    if max_cos < 0.1 and filterSquares(squares, cnt):
                        squares.append(cnt)

    return squares

def intersectionRate(s1, s2):
    area, intersection = cv2.intersectConvexConvex(np.array(s1), np.array(s2))
    return 2 * area / (cv2.contourArea(np.array(s1)) + cv2.contourArea(np.array(s2)))

def filterSquares(squares, square):

    for i in range(len(squares)):
        if intersectionRate(squares[i], square) > 0.95:
            return False

    return True

from tests_common import NewOpenCVTests

class squares_test(NewOpenCVTests):

    def test_squares(self):

        img = self.get_sample('samples/data/pic1.png')
        squares = find_squares(img)

        testSquares = [
        [[43, 25],
        [43, 129],
        [232, 129],
        [232, 25]],

        [[252, 87],
        [324, 40],
        [387, 137],
        [315, 184]],

        [[154, 178],
        [196, 180],
        [198, 278],
        [154, 278]],

        [[0, 0],
        [400, 0],
        [400, 300],
        [0, 300]]
        ]

        matches_counter = 0
        for i in range(len(squares)):
            for j in range(len(testSquares)):
                if intersectionRate(squares[i], testSquares[j]) > 0.9:
                    matches_counter += 1

        self.assertGreater(matches_counter / len(testSquares), 0.9)
        self.assertLess( (len(squares) - matches_counter) / len(squares), 0.2)