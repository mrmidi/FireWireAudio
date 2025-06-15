# Makefile for Analyzer testing

.PHONY: test test-verbose test-coverage install-deps clean help

# Default Python interpreter
PYTHON := python3

help:  ## Show this help message
	@echo "Available targets:"
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | awk 'BEGIN {FS = ":.*?## "}; {printf "  %-15s %s\n", $$1, $$2}'

install-deps:  ## Install testing dependencies
	$(PYTHON) -m pip install -r requirements.txt

test:  ## Run tests
	$(PYTHON) -m pytest tests/ -v

test-verbose:  ## Run tests with verbose output
	$(PYTHON) -m pytest tests/ -v -s

test-coverage:  ## Run tests with coverage report
	$(PYTHON) -m pytest tests/ -v --cov=firewire --cov-report=term-missing --cov-report=html:htmlcov

test-quick:  ## Run tests without coverage
	$(PYTHON) -m pytest tests/ -x

clean:  ## Clean up test artifacts
	rm -rf htmlcov/
	rm -rf .pytest_cache/
	rm -rf __pycache__/
	find . -name "*.pyc" -delete
	find . -name "*.pyo" -delete
	find . -name ".coverage" -delete

syntax-check:  ## Check syntax of Python files
	$(PYTHON) syntax_test.py

lint:  ## Run linting (if available)
	@if command -v ruff >/dev/null 2>&1; then \
		ruff check firewire/ tests/; \
	elif command -v flake8 >/dev/null 2>&1; then \
		flake8 firewire/ tests/; \
	else \
		echo "No linter available (install ruff or flake8)"; \
	fi