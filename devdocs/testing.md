# testing - how test driven development should proceed
for each feature developed, it is ideal that we:
1. work off of a new local feature/fix/etc branch
2. develop the feature, and a sound test for it to add to the test suite
3. when CI or build occurs these tests should all be ran.

when adding a new test it is CRITICAL that you do **NOT** under any circumstances satisfy the test with cheating, shortcuts, TODOs, or "sample implentations" ever!