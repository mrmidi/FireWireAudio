# See if any branch contains a tracked file with DebugTools in its path
for branch in $(git for-each-ref --format='%(refname:short)' refs/heads/); do
  git ls-tree -r --name-only "$branch" | grep -q DebugTools && echo "$branch"
done
