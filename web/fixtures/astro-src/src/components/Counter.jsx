import { useState } from 'preact/hooks';

export default function Counter({ id, label }) {
  const [count, setCount] = useState(0);
  return <button id={id} onClick={() => setCount(count + 1)}>{label} {count}</button>;
}
