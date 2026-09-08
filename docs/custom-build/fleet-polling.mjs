export function startFleetPolling(isActive, refresh, schedule = setTimeout) {
  const poll = async () => {
    try {
      if (isActive()) await refresh();
    } finally {
      schedule(poll, 30000);
    }
  };
  schedule(poll, 30000);
}
