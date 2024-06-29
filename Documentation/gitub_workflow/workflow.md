## Pulling the latest changes from main

```
git checkout main
git pull
```

## Starting a new feature branch

Different features should be seperated into different branches

1. to prevent code from different features being developed simulatenously from conflicting
2. to prevent broken code from ever entering the main branch

Features should first be pushed to the `develop` branch to be tested before being pushed to `main`

```
git checkout -b feature/feature-name

or alternativly

git checkout -b bugfix/bug-name
```

## Pushing changes to the feature branch to keep a backup in the central repo

```
git add file-name
git commit
git push -u origin feature/featrure-name
```
