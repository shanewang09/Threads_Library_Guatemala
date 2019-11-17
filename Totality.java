
public class Totality {
	public int sum(int[] a, String stype) {

		/*
		 * if (stype.equals("odd"))
		 * else if (stype.equals("even"))
		 */
		/*
		int esum = 0; 
		int osum = 0; 
		
		for (int k = 0; k < a.length; k += 1) {
			if (k % 2 == 0) {
				esum += a[k];
			} 
			
			else {
				osum += a[k];
			}
		}
		 
		int sum = osum + esum;
		return sum; 
		*/
		
		
		int sum = 0;

		if (stype.equals("all")) {
			for (int k = 0; k < a.length; k += 1) {
				sum += a[k];
			}
		}

		else if (stype.equals("even")) {
			for (int i = 0; i < a.length; i += 2) {
				sum += a[i];
			}
		}

		else if (stype.equals("odd")) {
			for (int j = 1; j < a.length; j += 2) {
				sum += a[j];
			}
		}
		
		else {
			return 0;
		}
		
		return sum;
		
	}
	
}
