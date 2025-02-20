import unittest

from charm.schemes.chamhash_rsa_hw09 import ChamHash_HW09
from charm.toolbox.integergroup import integer

debug = False


class ChamHash_HW09Test(unittest.TestCase):
    def testChamHash_HW09(self):
        # test p and q primes for unit tests only
        p = integer(
            164960892556379843852747960442703555069442262500242170785496141408191025653791149960117681934982863436763270287998062485836533436731979391762052869620652382502450810563192532079839617163226459506619269739544815249458016088505187490329968102214003929285843634017082702266003694786919671197914296386150563930299)
        q = integer(
            82480446278189921926373980221351777534721131250121085392748070704095512826895574980058840967491431718381635143999031242918266718365989695881026434810326191251225405281596266039919808581613229753309634869772407624729008044252593745164984051107001964642921817008541351133001847393459835598957148193075281965149)

        chamHash = ChamHash_HW09()
        (pk, sk) = chamHash.paramgen(1024, p, q)

        msg = "Hello world this is the message!"
        (h, r) = chamHash.hash(pk, msg)
        if debug: print("Hash...")
        if debug: print("sig =>", h)

        (h1, r1) = chamHash.hash(pk, msg, r)
        if debug: print("sig 2 =>", h1)

        assert h == h1, "Signature failed!!!"
        if debug: print("Signature generated correctly!!!")
