import unittest

from charm.schemes.grpsig.groupsig_bgls04 import ShortSig as BGLS04
from charm.toolbox.pairinggroup import PairingGroup

debug = False


class BGLS04Test(unittest.TestCase):
    def testBGLS04(self):
        groupObj = PairingGroup('MNT224')
        n = 3    # how manu users in the group
        user = 1 # which user's key to sign a message with

        sigTest = BGLS04(groupObj)

        (gpk, gmsk, gsk) = sigTest.keygen(n)

        message = 'Hello World this is a message!'
        if debug: print("\n\nSign the following M: '%s'" % (message))

        signature = sigTest.sign(gpk, gsk[user], message)

        result = sigTest.verify(gpk, message, signature)
        #if result:
        #    print("Verify signers identity...")
        #    index = sigTest.open(gpk, gmsk, message, signature)
        #    i = 0
        #    while i < n:
        #        if gsk[i][0] == index:
        #            print('Found index of signer: %d' % i)
        #            print('A = %s' % index)
        #        i += 1
        assert result, "Signature Failed"
        if debug: print('Complete!')